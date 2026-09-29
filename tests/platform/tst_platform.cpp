// The platform layer: what differs by system, each piece on its own.
#include "gigchain/platform/MemoryUse.h"
#include "gigchain/platform/PluginFolders.h"
#include "gigchain/platform/Timing.h"
#include "gigchain/platform/Windows.h"

#include <QDir>
#include <QWindow>
#include <QtTest>

#include <thread>

using namespace gigchain;
using namespace Qt::StringLiterals;

class TestPlatform : public QObject
{
    Q_OBJECT

private slots:
    void theStandardPluginFoldersAreAbsolute()
    {
        const QStringList folders = platform::standardVst3Folders();
        QVERIFY(!folders.isEmpty());
        for (const QString& folder : folders) QVERIFY2(QDir::isAbsolutePath(folder), qPrintable(folder));
#ifdef Q_OS_WIN
        QCOMPARE(folders, QStringList{u"C:/Program Files/Common Files/VST3"_s});
#else
        QVERIFY(folders.contains(QDir::homePath() + u"/.vst3"_s));
        QVERIFY(folders.contains(u"/usr/lib/vst3"_s));
        QVERIFY(folders.contains(u"/usr/local/lib/vst3"_s));
#endif
    }

    // Where a bundle keeps this system's binary (a fingerprint's file).
    void aBundlesModuleIsThisSystems()
    {
#ifdef Q_OS_WIN
        QCOMPARE(platform::vst3ModuleFile(u"C:/VST3/Synth.vst3"_s), u"C:/VST3/Synth.vst3/Contents/x86_64-win/Synth.vst3"_s);
        QCOMPARE(platform::fileNameCase(), Qt::CaseInsensitive);
#else
        QCOMPARE(platform::vst3ModuleFile(u"/usr/lib/vst3/Surge XT.vst3"_s),
                 u"/usr/lib/vst3/Surge XT.vst3/Contents/x86_64-linux/Surge XT.so"_s);
        QCOMPARE(platform::fileNameCase(), Qt::CaseSensitive);
#endif
    }

    void theNativeWindowKindIsThisSystems()
    {
#ifdef Q_OS_WIN
        QCOMPARE(platform::nativeWindowKind(), platform::NativeWindowKind::Win32);
#else
        QCOMPARE(platform::nativeWindowKind(), platform::NativeWindowKind::X11);
#endif
    }

    // Off-screen there is nothing to see: it must not fail, crash or throw.
    void bringingAWindowForwardKeepsItShown()
    {
        QWindow window;
        window.resize(100, 100);
        window.show();
        platform::bringToFront(window);
        QVERIFY(window.isVisible());
    }

    // A thread asking for precise timing keeps running normally.
    void preciseTimingIsAvailable()
    {
        bool ran = false;
        std::thread worker([&ran] {
            platform::preciseTimingForThisThread();
            ran = true;
            platform::endPreciseTimingForThisThread();
        });
        worker.join();
        QVERIFY(ran);
    }

    void theMemoryInUseIsRead()
    {
        const auto bytes = platform::residentBytes();
        QVERIFY2(bytes.has_value(), bytes ? "" : qPrintable(bytes.error().message));
        QVERIFY(*bytes > 1024LL * 1024); // a Qt test program: megabytes
        QVERIFY(*bytes < 64LL * 1024 * 1024 * 1024);
    }
};

QTEST_MAIN(TestPlatform)
#include "tst_platform.moc"
