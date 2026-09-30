// The host's run loop for plugin editors on Linux: a plugin's timers and its
// file handlers run on the main thread, stop when it says so, and all stop
// when its editor closes, whatever the plugin forgot.
#include "Vst3RunLoop.h"

#include "pluginterfaces/gui/iplugview.h"

#include <QtTest>

#include <atomic>

#include <unistd.h>

using namespace gigchain::engine;
using namespace Steinberg;

namespace {

// A plugin's handler: counts its calls; reference counted as plugins' are.
template <typename Interface>
class Counting : public Interface
{
public:
    int calls = 0;
    [[nodiscard]] int references() const { return m_references; }

    tresult PLUGIN_API queryInterface(const TUID requested, void** object) override
    {
        QUERY_INTERFACE(requested, object, FUnknown::iid, Interface)  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        QUERY_INTERFACE(requested, object, Interface::iid, Interface) // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return static_cast<uint32>(++m_references); }
    uint32 PLUGIN_API release() override { return static_cast<uint32>(--m_references); } // owned by the test

private:
    std::atomic<int> m_references{1};
};

class Timer final : public Counting<Linux::ITimerHandler>
{
public:
    void PLUGIN_API onTimer() override { ++calls; }
};

class Reader final : public Counting<Linux::IEventHandler>
{
public:
    void PLUGIN_API onFDIsSet(Linux::FileDescriptor fd) override
    {
        char byte = 0;
        if (::read(fd, &byte, 1) == 1) ++calls; // taken, so it is not "readable" again
    }
};

} // namespace

class TestVst3RunLoop : public QObject
{
    Q_OBJECT

private slots:
    void aTimerFiresUntilUnregistered()
    {
        const auto loop = makeVst3RunLoop();
        QVERIFY(loop != nullptr);
        Timer timer;
        QCOMPARE(loop->runLoop()->registerTimer(&timer, 10), kResultOk);
        QTRY_VERIFY(timer.calls >= 3);
        QCOMPARE(loop->runLoop()->unregisterTimer(&timer), kResultOk);
        const int calls = timer.calls;
        QTest::qWait(60);
        QCOMPARE(timer.calls, calls);
        QCOMPARE(timer.references(), 1); // let go when unregistered
    }

    void aFileHandlerRunsWhenThereIsSomethingToRead()
    {
        const auto loop = makeVst3RunLoop();
        std::array<int, 2> ends{};
        QCOMPARE(::pipe(ends.data()), 0);
        Reader reader;
        QCOMPARE(loop->runLoop()->registerEventHandler(&reader, ends.at(0)), kResultOk);
        QCOMPARE(::write(ends.at(1), "x", 1), ssize_t{1});
        QTRY_COMPARE(reader.calls, 1);
        QCOMPARE(loop->runLoop()->unregisterEventHandler(&reader), kResultOk);
        QCOMPARE(::write(ends.at(1), "y", 1), ssize_t{1});
        QTest::qWait(60);
        QCOMPARE(reader.calls, 1); // unregistered: not called again
        ::close(ends.at(0));
        ::close(ends.at(1));
    }

    // The plugin forgot its timer; its editor closes: nothing calls into it
    // again, and the loop lets go of it.
    void closingTheEditorStopsItsTimers()
    {
        const auto loop = makeVst3RunLoop();
        Timer timer;
        QCOMPARE(loop->runLoop()->registerTimer(&timer, 10), kResultOk);
        QTRY_VERIFY(timer.calls >= 1);
        loop->clear();
        const int calls = timer.calls;
        QTest::qWait(60);
        QCOMPARE(timer.calls, calls);
        QCOMPARE(timer.references(), 1);
    }

    void nothingRegisteredTwiceOrUnknownIsAccepted()
    {
        const auto loop = makeVst3RunLoop();
        Timer timer;
        QCOMPARE(loop->runLoop()->registerTimer(nullptr, 10), kInvalidArgument);
        QCOMPARE(loop->runLoop()->unregisterTimer(&timer), kInvalidArgument); // never registered
        Reader reader;
        QCOMPARE(loop->runLoop()->registerEventHandler(&reader, -1), kInvalidArgument);
    }
};

QTEST_GUILESS_MAIN(TestVst3RunLoop)
#include "tst_vst3_run_loop.moc"
