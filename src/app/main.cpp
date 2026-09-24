// OpenStage entry point and composition root: the only place that decides
// which engine runs and wires it to the UI.
#include "Session.h"
#include "SettingsController.h"
#include "StartupProgress.h"

#include "openstage/core/FileLog.h"
#include "openstage/engine/FakeEngineFactory.h"
#include "openstage/engine/RealEngineFactory.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <memory>

Q_IMPORT_QML_PLUGIN(OpenStage_UiPlugin)
Q_LOGGING_CATEGORY(lcApp, "openstage.app")

using namespace openstage;
using namespace Qt::StringLiterals;

namespace {

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
    QGuiApplication::setOrganizationName(u"OpenStage"_s);
    QGuiApplication::setApplicationName(u"OpenStage"_s);
    QGuiApplication::setApplicationVersion(u"0.1.0"_s);

    const LogScope logScope;
    const QString logPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/logs/openstage.log"_s;
    if (auto log = core::FileLog::install(logPath); !log) {
        qCWarning(lcApp).noquote() << log.error().message; // still reaches the debugger output
    } else {
        qCInfo(lcApp).noquote() << "OpenStage" << QGuiApplication::applicationVersion() << "starting; log:" << logPath;
    }

    QQuickStyle::setStyle(u"Basic"_s); // fully themeable by Theme.qml
    QSettings settings;

    // Splash first: opening audio, scanning plugins and loading the last
    // setlist's sounds all happen before the main window appears.
    ui::StartupProgress startup;
    bool starting = true; // progress is shown only until the main window is up
    auto splash = std::make_unique<QQmlApplicationEngine>();
    splash->setInitialProperties({{u"startup"_s, QVariant::fromValue(&startup)}});
    splash->loadFromModule(u"OpenStage.Ui"_s, u"Splash"_s);
    if (splash->rootObjects().isEmpty()) qCWarning(lcApp) << "The splash screen failed to load"; // not fatal
    startup.report(QGuiApplication::tr("Opening audio and MIDI"));

    engine::RealEngineOptions engineOptions = ui::SettingsController::engineOptions(settings);
    engineOptions.pluginCacheFile =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/plugin-cache.json"_s;
    engineOptions.progress = [&startup, &starting](const QString& what, int done, int total) {
        if (!starting) return;
        if (total > 0) {
            startup.report(QGuiApplication::tr("Scanning plugins (%1 of %2)").arg(done + 1).arg(total), what,
                           static_cast<double>(done) / total);
        } else {
            startup.report(QGuiApplication::tr("Loading sounds"), what);
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

    // "openstage.exe <setlist>" opens that file; otherwise reopen the last one.
    // Its sounds load now, behind the splash, not in a frozen main window.
    startup.report(QGuiApplication::tr("Loading your setlist"));
    const QStringList arguments = QGuiApplication::arguments();
    if (arguments.size() > 1) {
        (void)session.document().open(arguments.at(1)); // a failure is shown in the banner and logged
    } else {
        session.document().restoreLastSession();
    }

    startup.report(QGuiApplication::tr("Opening the window"));
    QQmlApplicationEngine qml;
    QObject::connect(&qml, &QQmlApplicationEngine::warnings, &app, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) qCWarning(lcApp).noquote() << warning.toString();
    });
    qml.setInitialProperties(session.initialProperties());
    qml.loadFromModule(u"OpenStage.Ui"_s, u"Main"_s);
    if (qml.rootObjects().isEmpty()) {
        qCCritical(lcApp) << "The main window failed to load";
        return 1;
    }
    starting = false;
    splash.reset(); // the main window is up
    const int code = QGuiApplication::exec();
    qCInfo(lcApp) << "OpenStage exiting with code" << code;
    return code;
}
