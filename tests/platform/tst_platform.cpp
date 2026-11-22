// The platform layer: what differs by system, each piece on its own.
#include "gigchain/platform/Audio.h"
#include "gigchain/platform/MemoryUse.h"
#include "gigchain/platform/PluginFolders.h"
#include "gigchain/platform/Timing.h"
#include "gigchain/platform/Windows.h"

#include <QDir>
#include <QGuiApplication>
#include <QWindow>
#include <QtTest>

#include <limits>
#include <optional>
#include <thread>

#ifdef Q_OS_LINUX
#include <sys/resource.h>
#endif

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
#elif defined(Q_OS_MACOS)
        QCOMPARE(folders, (QStringList{QDir::homePath() + u"/Library/Audio/Plug-Ins/VST3"_s, u"/Library/Audio/Plug-Ins/VST3"_s}));
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
#elif defined(Q_OS_MACOS)
        QCOMPARE(platform::vst3ModuleFile(u"/Library/Audio/Plug-Ins/VST3/Surge XT.vst3"_s),
                 u"/Library/Audio/Plug-Ins/VST3/Surge XT.vst3/Contents/MacOS/Surge XT"_s);
        QCOMPARE(platform::fileNameCase(), Qt::CaseInsensitive);
#else
        QCOMPARE(platform::vst3ModuleFile(u"/usr/lib/vst3/Surge XT.vst3"_s),
                 u"/usr/lib/vst3/Surge XT.vst3/Contents/x86_64-linux/Surge XT.so"_s);
        QCOMPARE(platform::fileNameCase(), Qt::CaseSensitive);
#endif
    }

    // The kind of window a plugin gets is the one the app really runs on:
    // on Linux X11 only through Qt's X11 platform (xcb); on Wayland, or
    // off-screen as here, there is none for plugins (said, not guessed).
    void theNativeWindowKindIsThisSystems()
    {
#ifdef Q_OS_WIN
        QCOMPARE(platform::nativeWindowKind(), platform::NativeWindowKind::Win32);
#else
        QCOMPARE(QGuiApplication::platformName(), u"offscreen"_s); // (tests run off-screen)
        QCOMPARE(platform::nativeWindowKind(), platform::NativeWindowKind::None);
#endif
    }

    // Off-screen there is nothing to see: it must not fail, crash or throw.
    // Plugin windows in the system's units: pixels where plugins size in
    // pixels (Windows, Linux: the screen's ratio, and they are told the
    // scale), points on the Mac (1.0, and the system scales them).
    void pluginWindowsUseTheSystemsUnits()
    {
        QWindow window;
        window.resize(100, 100);
        window.create();
        QCOMPARE(window.devicePixelRatio(), 2.0);
#ifdef Q_OS_MACOS
        QCOMPARE(platform::pluginPixelRatio(window), 1.0);
        QVERIFY(!platform::pluginsTakeContentScale());
#else
        QCOMPARE(platform::pluginPixelRatio(window), 2.0);
        QVERIFY(platform::pluginsTakeContentScale());
#endif
    }

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

    // The audio thread flushes denormal floats to zero (a decaying reverb
    // tail otherwise costs huge CPU), on x86 and on Apple Silicon alike.
    void denormalsAreFlushedToZero()
    {
        float result = -1.0F;
        std::thread audio([&result] {
            platform::flushDenormalsToZeroForThisThread();
            volatile float smallest = std::numeric_limits<float>::min(); // the smallest normal float
            result = smallest / 4.0F; // a denormal, unless flushed
        });
        audio.join();
        QCOMPARE(result, 0.0F);
    }

    void theMemoryInUseIsRead()
    {
        const auto bytes = platform::residentBytes();
        QVERIFY2(bytes.has_value(), bytes ? "" : qPrintable(bytes.error().message));
        QVERIFY(*bytes > 1024LL * 1024); // a Qt test program: megabytes
        QVERIFY(*bytes < 64LL * 1024 * 1024 * 1024);
    }

    // Linux: the realtime priority the session allows this process
    // (RLIMIT_RTPRIO), read as the system reports it (#71). Windows and the
    // Mac have no such limit: any app may ask for a realtime audio thread.
    void theRealtimePriorityLimitIsThisSystems()
    {
        // (A limit that cannot be read is a failure with its cause, not "no limit".)
        const core::Result<std::optional<int>> limit = platform::realtimePriorityLimit();
        QVERIFY2(limit.has_value(), limit ? "" : qPrintable(limit.error().message));
#ifdef Q_OS_LINUX
        rlimit saved{};
        QCOMPARE(getrlimit(RLIMIT_RTPRIO, &saved), 0);
        QVERIFY(limit->has_value());
        QCOMPARE(**limit, saved.rlim_cur == RLIM_INFINITY ? std::numeric_limits<int>::max() : static_cast<int>(saved.rlim_cur));
        // A session that grants none (a user not in the pipewire or audio
        // group): 0. (Lowering the soft limit needs no rights; it is put back.)
        rlimit none = saved;
        none.rlim_cur = 0;
        QCOMPARE(setrlimit(RLIMIT_RTPRIO, &none), 0);
        const core::Result<std::optional<int>> withNone = platform::realtimePriorityLimit();
        QCOMPARE(setrlimit(RLIMIT_RTPRIO, &saved), 0);
        QVERIFY(withNone.has_value());
        QCOMPARE(*withNone, std::optional<int>(0));
#else
        QVERIFY(!limit->has_value());
#endif
    }

    // The player is told only when the session grants no realtime priority,
    // and told what to do about it: the app cannot give it to itself.
    void noRealtimePriorityIsExplained()
    {
        const QString advice = platform::realtimePriorityAdvice(0);
        QVERIFY(advice.contains(u"realtime priority"_s));
        // A limit of 0 is a risk, not a measurement: PipeWire can still get
        // realtime threads through RTKit, so it says "can", not "does".
        QVERIFY2(advice.contains(u"limit"_s) && advice.contains(u"can drop out"_s), qPrintable(advice));
        QVERIFY2(!advice.contains(u"has no realtime priority"_s), qPrintable(advice));
        QVERIFY2(advice.contains(u"usermod -aG pipewire $USER"_s) && advice.contains(u"-aG audio"_s), qPrintable(advice));
        // Read in a message at the window's corner: short enough to be whole there.
        QVERIFY2(advice.size() <= 200, qPrintable(u"%1 characters"_s.arg(advice.size())));
        QVERIFY2(advice.contains(u"pipewire"_s) && advice.contains(u"audio"_s), qPrintable(advice)); // the groups
        QVERIFY2(advice.contains(u"log in again"_s), qPrintable(advice)); // (a group counts from the next login)
        QVERIFY(platform::realtimePriorityAdvice(95).isEmpty());
        QVERIFY(platform::realtimePriorityAdvice(std::numeric_limits<int>::max()).isEmpty());
        QVERIFY(platform::realtimePriorityAdvice(std::nullopt).isEmpty()); // (Windows, the Mac)
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_SCALE_FACTOR", "2"); // a "Retina" screen, off-screen
    QGuiApplication app(argc, argv);
    TestPlatform test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_platform.moc"
