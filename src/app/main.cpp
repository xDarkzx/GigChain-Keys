// Entry point and composition root: the only place that decides which engine
// runs and wires it to the UI. The product's name comes from branding.cmake.
#include "Session.h"
#include "SettingsController.h"
#include "SettingsMigration.h"
#include "StageQuips.h"
#include "StartupProgress.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/FileLog.h"
#include "gigchain/engine/FakeEngineFactory.h"
#include "gigchain/engine/RealEngineFactory.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <chrono>
#include <memory>

Q_IMPORT_QML_PLUGIN(GigChain_UiPlugin)
Q_LOGGING_CATEGORY(lcApp, "gigchain.app")

using namespace gigchain;
using namespace Qt::StringLiterals;

namespace {

// The splash stays up at least this long, even when everything loads faster
// (the plugin list usually comes from the cache in milliseconds).
constexpr qint64 kMinimumSplashMs = 10'000;

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

} // namespace

int main(int argc, char* argv[])
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // Report leaks to the debugger output at exit during development.
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
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

    QQuickStyle::setStyle(u"Basic"_s); // fully themeable by Theme.qml
    QSettings settings;
    ui::carryOverPreviousSettings(settings); // after a rename: nothing to set up again

    // Splash first: opening audio, scanning plugins and loading the last
    // setlist's sounds all happen before the main window appears.
    ui::StartupProgress startup;
    bool starting = true; // progress is shown only until the main window is up
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
    engineOptions.progress = [&startup, &starting, &quips](const QString& what, int done, int total) {
        if (!starting) return;
        if (total > 0) { // scanning plugins
            startup.addPlugin(what);
            startup.report(quips.line(Quip::Unpacking), what, static_cast<double>(done) / total);
        } else { // loading a sound for the setlist
            startup.report(quips.line(Quip::WarmingUp), what);
        }
    };

    std::unique_ptr<engine::IEngine> engine;
    QString engineProblem;
    if (auto real = engine::createRealEngine(engineOptions)) {
        engine = std::move(*real);
    } else {
        engineProblem = real.error().message;
        qCWarning(lcApp).noquote() << "Real engine unavailable, running the demo engine:" << engineProblem;
        engine = engine::createFakeEngine();
    }

    ui::Session session(*engine, settings);
    if (!engineProblem.isEmpty()) {
        session.document().reportMessage(QGuiApplication::tr("No audio output (%1). Running without sound.").arg(engineProblem));
    }

    // "<exe> <setlist>" opens that file; otherwise reopen the last one.
    // Its sounds load now, behind the splash, not in a frozen main window.
    startup.report(quips.line(Quip::Setlist)); // loading the setlist
    const QStringList arguments = QGuiApplication::arguments();
    if (arguments.size() > 1) {
        (void)session.document().open(arguments.at(1)); // a failure is shown in the banner and logged
    } else {
        session.document().restoreLastSession();
    }

    startup.report(quips.line(Quip::SoundGuy)); // opening the main window
    QQmlApplicationEngine qml;
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
    starting = false;

    // Swap the splash for the main window, brought to the front.
    const auto reveal = [&splash, mainWindow] {
        splash.reset();
        mainWindow->show();
        mainWindow->raise();
        mainWindow->requestActivate();
    };
    const qint64 remaining = kMinimumSplashMs - splashShown.elapsed();
    if (remaining > 0) {
        startup.finish(static_cast<int>(remaining), quips.line(Quip::LineCheck), quips.line(Quip::Ready));
        QTimer::singleShot(std::chrono::milliseconds(remaining), mainWindow, reveal);
    } else {
        reveal();
    }
    const int code = QGuiApplication::exec();
    qCInfo(lcApp).noquote() << branding::name() << "exiting with code" << code;
    return code;
}
