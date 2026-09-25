#include "PluginLoadGuard.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

class TestPluginLoadGuard : public QObject
{
    Q_OBJECT

    const QString kPiano = u"C:/Plugins/Test Plugin A.vst3"_s;
    const QString kSynth = u"C:/Plugins/Test Plugin B.vst3"_s;

private slots:
    void aLoadThatFinishesLeavesNothingBehind()
    {
        QTemporaryDir dir;
        {
            PluginLoadGuard guard(dir.path());
            const auto loading = guard.loading(kPiano);
        } // loaded (or failed cleanly): the marker goes
        PluginLoadGuard nextStart(dir.path());
        QVERIFY(nextStart.takeCrashed().isEmpty());
        QVERIFY(!nextStart.isBlocked(kPiano));
    }

    void aPluginThatCrashedTheAppIsBlockedNextTime()
    {
        QTemporaryDir dir;
        PluginLoadGuard crashed(dir.path());
        const auto loading = crashed.loading(kPiano); // the app "dies" here: the marker stays

        PluginLoadGuard nextStart(dir.path());
        QCOMPARE(nextStart.takeCrashed(), QStringList{kPiano});
        QVERIFY(nextStart.isBlocked(kPiano));
        QVERIFY(!nextStart.isBlocked(kSynth));
        QVERIFY(nextStart.takeCrashed().isEmpty()); // reported once

        PluginLoadGuard later(dir.path()); // still blocked at later starts
        QCOMPARE(later.blocked(), QStringList{kPiano});
        later.unblock(kPiano); // "Try again" in Settings
        QVERIFY(!later.isBlocked(kPiano));
        PluginLoadGuard afterThat(dir.path());
        QVERIFY(!afterThat.isBlocked(kPiano));
    }

    void pathsAreComparedAsWindowsDoes()
    {
        QTemporaryDir dir;
        PluginLoadGuard crashed(dir.path());
        const auto loading = crashed.loading(kPiano);
        PluginLoadGuard nextStart(dir.path());
        (void)nextStart.takeCrashed();
        QVERIFY(nextStart.isBlocked(u"c:\\plugins\\test plugin a.vst3"_s));
    }

    void withoutAFolderItDoesNothing()
    {
        PluginLoadGuard off;
        const auto loading = off.loading(kPiano);
        QVERIFY(off.takeCrashed().isEmpty());
        QVERIFY(!off.isBlocked(kPiano));
    }

    void aDamagedBlockListIsReportedNotTrusted()
    {
        QTemporaryDir dir;
        QFile file(QDir(dir.path()).filePath(u"blocked.json"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"blocked plugins list.*damaged"_s));
        PluginLoadGuard guard(dir.path());
        QVERIFY(guard.blocked().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPluginLoadGuard)
#include "tst_plugin_load_guard.moc"
