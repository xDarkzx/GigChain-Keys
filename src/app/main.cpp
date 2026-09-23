// OpenStage entry point and composition root: the only place that decides
// which engine runs and wires it to the UI.
#include "Session.h"

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

    std::unique_ptr<engine::IEngine> engine;
    QString engineProblem;
    if (auto real = engine::createRealEngine()) {
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

    session.document().restoreLastSession();
    const int code = QGuiApplication::exec();
    qCInfo(lcApp) << "OpenStage exiting with code" << code;
    return code;
}
