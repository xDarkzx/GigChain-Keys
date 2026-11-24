// Entry point and composition root: the only place that decides which engine
// runs and wires it to the UI. The product's name comes from branding.cmake.
#include "Session.h"
#include "SettingsController.h"
#include "CrashReports.h"
#include "FreezeWatchdog.h"
#include "Language.h"
#include "SettingsMigration.h"
#include "SingleInstance.h"
#include "StageQuips.h"
#include "StartupProgress.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/FileLog.h"
#include "gigchain/engine/FakeEngineFactory.h"
#include "gigchain/platform/Audio.h"
#include "gigchain/platform/Process.h"
#include "gigchain/platform/Windows.h"
#include "gigchain/engine/RealEngineFactory.h"

#ifdef GIGCHAIN_DEV_CONTROL
#include "DevControl.h"
#include "DevMarker.h"
#endif

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLocale>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QFileInfo>
#include <QDir>
#include <QScopeGuard>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>

Q_IMPORT_QML_PLUGIN(GigChain_UiPlugin)
Q_LOGGING_CATEGORY(lcApp, "gigchain.app")

using namespace gigchain;
using namespace Qt::StringLiterals;

namespace {

using platform::bringToFront;

// The splash stays up at least this long, even when everything loads faster
// (the plugin list usually comes from the cache in milliseconds).
constexpr qint64 kMinimumSplashMs = 10'000;
// The line check at the end of the splash (every plugin named) takes at least this long.
constexpr qint64 kLineCheckMs = 4'000;

// Closes the log file last, after everything that might still log is gone.
struct LogScope
{
    LogScope() = default;
    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;
    LogScope(LogScope&&) = delete;
    LogScope& operator=(LogScope&&) = delete;
    ~LogScope() { core::FileLog::uninstall(); }
};

int runApp(int argc, char** argv)
{
    // Before anything loads a DLL: none from the folder the app was started
    // in (a double-clicked setlist's). Logged if it cannot.
    const auto dllSearch = platform::hardenLibrarySearch();
    platform::reportLeaksAtExit();
    platform::prepareGuiPlatform();
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(branding::organization());
    QGuiApplication::setApplicationName(branding::name());
    QGuiApplication::setApplicationDisplayName(branding::name());
    QGuiApplication::setApplicationVersion(branding::version());

    const LogScope logScope;
    const QString logPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/logs/"_s + branding::executable() + u".log"_s;
    if (auto log = core::FileLog::install(logPath); !log) {
        qCWarning(lcApp).noquote() << log.error().message; // still reaches the debugger output
    } else {
        qCInfo(lcApp).noquote() << branding::name() << branding::version() << "starting; log:" << logPath;
    }
    if (!dllSearch) qCWarning(lcApp).noquote() << dllSearch.error().message; // now in the log file too
#ifdef GIGCHAIN_DEV_CONTROL
    qCWarning(lcApp).noquote() << "A developer build with the live test control (" << devcontrol::marker() << "): never to be released";
#endif

    // Already running (a setlist double-clicked): the running app opens it.
    QStringList arguments = QGuiApplication::arguments();
#ifdef GIGCHAIN_DEV_CONTROL
    arguments.removeAll(u"--dev-control"_s); // (the live test control's switch, not a setlist)
#endif
    const QString given = arguments.size() > 1 ? QFileInfo(arguments.at(1)).absoluteFilePath() : QString();
    ui::SingleInstance instance(ui::SingleInstance::appName());
    if (!instance.first()) {
        if (instance.handOver(given)) {
            qCInfo(lcApp).noquote() << "Already running: handed over" << (given.isEmpty() ? u"(no setlist)"_s : given);
            return 0;
        }
        // Running but not answering (logged): starting anyway beats not starting.
        qCWarning(lcApp) << "Another start is running but did not answer: starting a second one";
    } else if (auto listening = instance.listen(); !listening) {
        qCWarning(lcApp).noquote() << listening.error().message;
    }
    // Handed over before the main window is up: done once it is.
    std::optional<QString> handedEarly;
    QObject::connect(&instance, &ui::SingleInstance::opened, &app, [&handedEarly](const QString& path) { handedEarly = path; });

    QQuickStyle::setStyle(u"Basic"_s); // fully themeable by Theme.qml
    QSettings settings;
    // A crash leaves a dump and a note of what the app was doing.
    ui::CrashReports::install(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                              + u"/crash-reports"_s);
    const auto crashReportsOff = qScopeGuard([] { ui::CrashReports::uninstall(); });
    const QStringList lastCrash = ui::CrashReports::takeNewReports();
    for (const QString& report : lastCrash) qCWarning(lcApp).noquote() << "The last run crashed; report:" << report;
    const ui::FreezeWatchdog watchdog; // logs any moment the window stops responding
    ui::carryOverPreviousSettings(settings); // after a rename: nothing to set up again

    // The language, before any text is shown (the splash's included): the
    // player's choice in Settings > General, else the computer's (docs/i18n/app.md).
    // It stays for the whole run; a failure is logged and the app speaks English.
    ui::Language language;
    if (const QString code = ui::Language::resolve(settings.value(ui::Language::kSettingKey).toString(),
                                                   QLocale().uiLanguages());
        !code.isEmpty()) {
        if (auto installed = language.install(code); !installed) {
            qCWarning(lcApp).noquote() << installed.error().message; // (installed: Language logs it)
        }
    }

    // Splash first: opening audio, scanning plugins and loading the last
    // setlist's sounds all happen before the main window appears.
    ui::StartupProgress startup;
    QElapsedTimer splashShown;
    splashShown.start();
    auto splash = std::make_unique<QQmlApplicationEngine>();
    splash->setInitialProperties({{u"startup"_s, QVariant::fromValue(&startup)}});
    splash->loadFromModule(u"GigChain.Ui"_s, u"Splash"_s);
    if (splash->rootObjects().isEmpty()) qCWarning(lcApp) << "The splash screen failed to load"; // not fatal
    // The splash speaks stage crew, not start-up steps (StageQuips).
    ui::StageQuips quips;
    using Quip = ui::StageQuips::Step;
    startup.report(quips.line(Quip::Connecting)); // opening audio and MIDI

    engine::RealEngineOptions engineOptions = ui::SettingsController::engineOptions(settings);
    engineOptions.pluginCacheFile =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/plugin-cache.json"_s;
    engineOptions.pluginGuardFolder =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/plugin-guard"_s;
    engineOptions.lastRunCrashed = !lastCrash.isEmpty(); // (closed or killed: not a plugin's crash)
    // Built next to the app (src/scanner): new plugins are read there.
    engineOptions.pluginScanner =
        QCoreApplication::applicationDirPath() + u"/"_s + platform::programFileName(branding::executable() + u"Scan"_s);
    // Instruments that come with the app, where an installer puts them.
    engineOptions.bundledPluginFolder = QCoreApplication::applicationDirPath() + u"/plugins"_s;
    engineOptions.progress = [&startup, &quips](engine::LoadStage stage, const QString& what, int done, int total) {
        if (stage == engine::LoadStage::ScanningPlugins) {
            if (done < total) startup.addPlugin(what);
            startup.report(quips.line(Quip::Unpacking), what, total > 0 ? static_cast<double>(done) / total : -1.0);
        } else {
            startup.report(quips.line(Quip::WarmingUp), what, total > 0 ? static_cast<double>(done) / total : -1.0);
        }
    };

    std::unique_ptr<engine::IEngine> engine;
    QString engineProblem;
    if (auto real = engine::createRealEngine(engineOptions)) {
        engine = std::move(*real);
    } else {
        // No audio at all: open anyway with the silent demo engine, so the
        // setlist and charts can still be read and edited (said on screen below).
        engineProblem = real.error().message;
        qCWarning(lcApp).noquote() << "Real engine unavailable, running the demo engine:" << engineProblem;
        engine = engine::createFakeEngine();
    }

    ui::Session session(*engine, settings);
    if (!lastCrash.isEmpty()) {
        session.document().reportMessage(
            QGuiApplication::tr("%1 closed unexpectedly last time. A crash report was saved in %2")
                .arg(branding::name(), QDir::toNativeSeparators(QFileInfo(lastCrash.back()).path())));
    }
    if (!engineProblem.isEmpty()) {
        session.document().reportMessage(QGuiApplication::tr("No audio output (%1). Running without sound.").arg(engineProblem));
    }
    // Linux: a session with no realtime priority runs the audio, and the
    // plugin hosts started for Wine plugins, at ordinary priority: about five
    // times the dropouts (#71). The system grants it, not the app: the player
    // is told how, at each start until it is fixed.
    // (Not read: logged with its cause there; nothing for the player to do.)
    if (const core::Result<std::optional<int>> realtimeLimit = platform::realtimePriorityLimit(); realtimeLimit) {
        if (*realtimeLimit) qCInfo(lcApp) << "Realtime priority limit (RLIMIT_RTPRIO):" << **realtimeLimit;
        if (const QString advice = platform::realtimePriorityAdvice(*realtimeLimit); !advice.isEmpty()) {
            qCWarning(lcApp).noquote() << advice;
            session.document().reportMessage(advice, ui::Notifications::Warning);
        }
    }

    // "<exe> <setlist>" opens that file; otherwise the last one reopens only
    // if the user chose that in Settings > General (else: the start screen).
    // Its sounds load now, behind the splash, not in a frozen main window.
    startup.report(quips.line(Quip::Setlist)); // loading the setlist
    if (!given.isEmpty()) {
        (void)session.document().open(given); // a failure is shown in the banner and logged
    } else {
        session.document().restoreLastSession();
    }
    // The last setlist reopened, and the player wants to start playing: Perform, once the window is up.
    const bool performAtStart = given.isEmpty() && !session.document().filePath().isEmpty()
                                && ui::SettingsController::startInPerform(settings);

    startup.report(quips.line(Quip::SoundGuy)); // opening the main window
    QQmlApplicationEngine qml;
    ui::PluginIconProvider::install(qml);
    QObject::connect(&qml, &QQmlApplicationEngine::warnings, &app, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) qCWarning(lcApp).noquote() << warning.toString();
    });
    QVariantMap properties = session.initialProperties();
    properties.insert(u"visible"_s, false); // shown when the splash is done
    qml.setInitialProperties(properties);
    qml.loadFromModule(u"GigChain.Ui"_s, u"Main"_s);
    auto* mainWindow = qml.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(qml.rootObjects().first());
    if (mainWindow == nullptr) {
        qCCritical(lcApp) << "The main window failed to load";
        return 1;
    }
    engine->setProgressHandler([&session](engine::LoadStage, const QString& what, int done, int total) {
        session.loading().loading(QGuiApplication::tr("Loading sounds…"), what, done, total);
    });
#ifdef GIGCHAIN_DEV_CONTROL
    // The live test control: only when started with --dev-control (docs/testing/live-test-control.md).
    const auto devControl = devcontrol::startIfAsked(
        QGuiApplication::arguments(),
        devcontrol::DevControl::Context{.engine = engine.get(),
                                        .objects = session.initialProperties(),
                                        .window = mainWindow,
                                        .logFile = logPath,
                                        .dataFolder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
                                        .socketName = {}});
#endif

    // A later start's setlist opens as a recent one does (asking first about
    // unsaved changes), and the window comes to the front.
    const auto openHanded = [mainWindow](const QString& path) {
        if (!path.isEmpty() &&
            !QMetaObject::invokeMethod(mainWindow, "openRecent", Q_ARG(QVariant, QVariant(path)))) {
            qCWarning(lcApp).noquote() << "Could not open" << path << "handed over by a later start: Main.openRecent is missing";
        }
        if (mainWindow->isVisible()) bringToFront(*mainWindow);
    };

    // Swap the splash for the main window, brought to the front.
    const auto reveal = [&splash, mainWindow, &instance, &handedEarly, openHanded, performAtStart] {
        mainWindow->show();
        if (performAtStart && !mainWindow->setProperty("performMode", true)) {
            qCWarning(lcApp) << "Could not start in Perform: Main.performMode is missing";
        }
        bringToFront(*mainWindow);
        splash.reset(); // after the main window is up: the app never loses the front
        QObject::disconnect(&instance, &ui::SingleInstance::opened, nullptr, nullptr);
        QObject::connect(&instance, &ui::SingleInstance::opened, mainWindow, openHanded);
        if (handedEarly) openHanded(*handedEarly);
    };
    // Always finish with the line check (every plugin named, the bar gliding
    // across), even when loading the setlist's sounds took longer than the
    // minimum splash time.
    const qint64 remaining = std::max(kMinimumSplashMs - splashShown.elapsed(), kLineCheckMs);
    startup.finish(static_cast<int>(remaining), quips.line(Quip::LineCheck), quips.line(Quip::Ready));
    QTimer::singleShot(std::chrono::milliseconds(remaining), mainWindow, reveal);
    const int code = QGuiApplication::exec();
    qCInfo(lcApp).noquote() << branding::name() << "exiting with code" << code;
    return code;
}

} // namespace

// Anything thrown out of the app is said, with what it was, instead of the
// process being ended without a word.
int main(int argc, char** argv)
{
    try {
        return runApp(argc, argv);
    } catch (const std::exception& e) {
        qCCritical(lcApp).noquote() << "Stopped by an unexpected error:" << e.what();
    } catch (...) {
        qCCritical(lcApp) << "Stopped by an unexpected error of an unknown kind";
    }
    return 1;
}
