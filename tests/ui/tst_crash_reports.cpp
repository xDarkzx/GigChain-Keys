#include "CrashReports.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestCrashReports : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { CrashReports::uninstall(); }

    void aReportHasADumpAndSaysWhatTheAppWasDoing()
    {
        QTemporaryDir dir;
        CrashReports::install(dir.path());
        CrashReports::setLastAction(u"switch to Test Song / Chorus"_s);
        const QString dump = CrashReports::writeNow("a test");
        QVERIFY(!dump.isEmpty());
        QVERIFY(QFileInfo(dump).size() > 1000); // a real minidump

        QFile note(QFileInfo(dump).path() + u'/' + QFileInfo(dump).completeBaseName() + u".txt"_s);
        QVERIFY(note.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(note.readAll());
        QVERIFY2(text.contains(u"Last action: switch to Test Song / Chorus"_s), qPrintable(text));
        QVERIFY(text.contains(u"Reason: a test"_s));
    }

    void theNextStartTellsOnceAboutNewReports()
    {
        QTemporaryDir dir;
        CrashReports::install(dir.path());
        const QString dump = CrashReports::writeNow("a test");
        QCOMPARE(CrashReports::takeNewReports(), QStringList{dump});
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
};

QTEST_GUILESS_MAIN(TestCrashReports)
#include "tst_crash_reports.moc"
