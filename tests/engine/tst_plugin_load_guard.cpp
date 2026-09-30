#include "PluginLoadGuard.h"

#include "gigchain/platform/PluginFolders.h"

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

    // Paths are compared as the system does: on Windows whatever the case and
    // the slashes; on the Mac whatever the case; on Linux two names differing
    // in case are two plugins.
    void pathsAreComparedAsTheSystemDoes()
    {
        QTemporaryDir dir;
        PluginLoadGuard crashed(dir.path());
        const auto loading = crashed.loading(kPiano);
        PluginLoadGuard nextStart(dir.path());
        (void)nextStart.takeCrashed();
#ifdef Q_OS_WIN
        QVERIFY(nextStart.isBlocked(u"c:\\plugins\\test plugin a.vst3"_s));
#else
        QVERIFY(nextStart.isBlocked(kPiano));
        // Where names differ by case they are other plugins (Linux); where
        // they do not, the same one (the Mac).
        QCOMPARE(nextStart.isBlocked(kPiano.toLower()), gigchain::platform::fileNameCase() == Qt::CaseInsensitive);
#endif
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
