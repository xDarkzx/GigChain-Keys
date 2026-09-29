#include "CrashReports.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

QString readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class TestCrashReports : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { CrashReports::uninstall(); }

    void aReportSaysWhatTheAppWasDoing()
    {
        QTemporaryDir dir;
        CrashReports::install(dir.path());
        CrashReports::setLastAction(u"switch to Test Song / Chorus"_s);
        const QString report = CrashReports::writeNow("a test");
        QVERIFY(!report.isEmpty());
#ifdef Q_OS_WIN
        QVERIFY(QFileInfo(report).size() > 1000); // a real minidump
#endif
        const QString text = readAll(CrashReports::noteOf(report));
        QVERIFY2(text.contains(u"Last action: switch to Test Song / Chorus"_s), qPrintable(text));
        QVERIFY(text.contains(u"Reason: a test"_s));
    }

    void theNextStartTellsOnceAboutNewReports()
    {
        QTemporaryDir dir;
        CrashReports::install(dir.path());
        const QString report = CrashReports::writeNow("a test");
        QCOMPARE(CrashReports::takeNewReports(), QStringList{report});
        QVERIFY(CrashReports::takeNewReports().isEmpty()); // told once

        CrashReports::uninstall(); // the next run
        CrashReports::install(dir.path());
        QVERIFY(CrashReports::takeNewReports().isEmpty());
    }

    void withoutAFolderNothingIsWritten()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Could not write a crash report"_s));
        QVERIFY(CrashReports::writeNow("not installed").isEmpty());
        QVERIFY(CrashReports::takeNewReports().isEmpty());
    }

    // A real crash (in a child: this test must go on) leaves a report the
    // next start finds, saying what the app was doing.
    void aCrashInAChildLeavesItsNote()
    {
        QTemporaryDir dir;
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({u"--crash-into"_s, dir.path()});
        child.start();
        QVERIFY(child.waitForFinished(30'000));
        QVERIFY(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0);

        CrashReports::install(dir.path());
        const QStringList reports = CrashReports::takeNewReports();
        QCOMPARE(reports.size(), 1);
        const QString text = readAll(CrashReports::noteOf(reports.front()));
        QVERIFY2(text.contains(u"Last action: about to crash"_s), qPrintable(text));
        QVERIFY2(text.contains(u"Reason: the app crashed"_s), qPrintable(text));
    }
};

int main(int argc, char** argv)
{
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    const auto crashInto = std::ranges::find_if(arguments, [](const char* a) { return std::string_view(a) == "--crash-into"; });
    if (crashInto != arguments.end() && std::next(crashInto) != arguments.end()) {
        const QCoreApplication app(argc, argv);
        CrashReports::install(QString::fromLocal8Bit(*std::next(crashInto)));
        CrashReports::setLastAction(u"about to crash"_s);
        volatile std::uintptr_t nowhere = 0;
        // cppcheck-suppress nullPointer ; the crash is the point
        *reinterpret_cast<volatile int*>(nowhere) = 1; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): the crash is the point
        return 0;
    }
    const QCoreApplication app(argc, argv);
    TestCrashReports test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_crash_reports.moc"
