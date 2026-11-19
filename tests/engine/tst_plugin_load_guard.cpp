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
        QVERIFY(nextStart.takeCrashed(true).isEmpty());
        QVERIFY(!nextStart.isBlocked(kPiano));
    }

    void aPluginThatCrashedTheAppIsBlockedNextTime()
    {
        QTemporaryDir dir;
        PluginLoadGuard crashed(dir.path());
        const auto loading = crashed.loading(kPiano); // the app "dies" here: the marker stays

        PluginLoadGuard nextStart(dir.path());
        QCOMPARE(nextStart.takeCrashed(true), QStringList{kPiano});
        QVERIFY(nextStart.isBlocked(kPiano));
        QVERIFY(!nextStart.isBlocked(kSynth));
        QVERIFY(nextStart.takeCrashed(true).isEmpty()); // reported once

        PluginLoadGuard later(dir.path()); // still blocked at later starts
        QCOMPARE(later.blocked(), QStringList{kPiano});
        later.unblock(kPiano); // "Try again" in Settings
        QVERIFY(!later.isBlocked(kPiano));
        PluginLoadGuard afterThat(dir.path());
        QVERIFY(!afterThat.isBlocked(kPiano));
    }

    // The app closed or killed while a plugin loads (no crash report): not the
    // plugin's fault, so it stays on (#55).
    void aLoadCutShortWithoutACrashLeavesThePluginOn()
    {
        QTemporaryDir dir;
        PluginLoadGuard killed(dir.path());
        const auto loading = killed.loading(kPiano);

        PluginLoadGuard nextStart(dir.path());
        QVERIFY(nextStart.takeCrashed(false).isEmpty());
        QVERIFY(!nextStart.isBlocked(kPiano));
        PluginLoadGuard later(dir.path());
        QVERIFY(later.blocked().isEmpty());
    }

    // A plugin that takes the app down without a crash report (it never gets
    // to write one) is caught the second time in a row: never a crash at every start.
    void aLoadCutShortTwiceInARowIsBlocked()
    {
        QTemporaryDir dir;
        {
            PluginLoadGuard first(dir.path());
            const auto loading = first.loading(kPiano);
            PluginLoadGuard second(dir.path());
            QVERIFY(second.takeCrashed(false).isEmpty());
            const auto again = second.loading(kPiano); // the app goes down again here
            PluginLoadGuard third(dir.path());
            QCOMPARE(third.takeCrashed(false), QStringList{kPiano});
            QVERIFY(third.isBlocked(kPiano));
        }
    }

    // Once it loads fine, an earlier cut-short load is forgotten.
    void aLoadThatFinishesClearsAnEarlierCutShortOne()
    {
        QTemporaryDir dir;
        PluginLoadGuard first(dir.path());
        const auto cutShort = first.loading(kPiano);
        PluginLoadGuard second(dir.path());
        QVERIFY(second.takeCrashed(false).isEmpty());
        { const auto loaded = second.loading(kPiano); } // loads fine
        const auto cutShortAgain = second.loading(kPiano);
        PluginLoadGuard third(dir.path());
        QVERIFY(third.takeCrashed(false).isEmpty()); // the first time again
        QVERIFY(!third.isBlocked(kPiano));
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
        (void)nextStart.takeCrashed(true);
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
        QVERIFY(off.takeCrashed(true).isEmpty());
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
