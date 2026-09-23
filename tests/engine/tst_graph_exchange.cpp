#include "GraphExchange.h"
#include "MidiQueue.h"

#include <QtTest>

#include <atomic>
#include <thread>

using namespace openstage::engine;

namespace {

std::shared_ptr<RenderGraph> emptyGraph()
{
    return std::make_shared<RenderGraph>(std::vector<StripSpec>{}, 48000.0, 64);
}

} // namespace

class TestGraphExchange : public QObject
{
    Q_OBJECT

private slots:
    void audioSeesLatestGraph()
    {
        GraphExchange exchange;
        QVERIFY(exchange.acquire() == nullptr);
        exchange.release();

        auto first = emptyGraph();
        exchange.publish(first);
        QVERIFY(exchange.acquire() == first.get());
        exchange.release();

        auto second = emptyGraph();
        exchange.publish(second);
        QVERIFY(exchange.acquire() == second.get());
        exchange.release();
        QVERIFY(exchange.current() == second.get());
    }

    void graphInUseIsNotFreed()
    {
        GraphExchange exchange;
        auto first = emptyGraph();
        const std::weak_ptr<RenderGraph> watch = first;
        exchange.publish(std::move(first));

        RenderGraph* inUse = exchange.acquire(); // audio thread mid-block
        exchange.publish(emptyGraph());
        exchange.collectGarbage();
        QVERIFY(!watch.expired());
        QVERIFY(inUse != nullptr);

        exchange.release();
        exchange.collectGarbage();
        QVERIFY(watch.expired());
    }

    void destructionFreesEverything()
    {
        std::weak_ptr<RenderGraph> watch;
        {
            GraphExchange exchange;
            auto graph = emptyGraph();
            watch = graph;
            exchange.publish(std::move(graph));
            exchange.publish(emptyGraph());
        }
        QVERIFY(watch.expired());
    }

    void concurrentPublishIsSafe()
    {
        // Run under the asan preset: a use-after-free here is reported there.
        GraphExchange exchange;
        exchange.publish(emptyGraph());
        std::atomic<bool> stop{false};
        std::atomic<long> blocks{0};
        std::thread audio([&] {
            std::vector<float> left(64);
            std::vector<float> right(64);
            while (!stop.load()) {
                if (RenderGraph* graph = exchange.acquire()) {
                    graph->render({}, AudioBlock{left.data(), right.data(), 64}, 1.0F);
                }
                exchange.release();
                blocks.fetch_add(1);
            }
        });
        for (int i = 0; i < 5000; ++i) {
            exchange.publish(emptyGraph());
            exchange.collectGarbage();
        }
        stop = true;
        audio.join();
        exchange.collectGarbage();
        QVERIFY(blocks.load() > 0);
        QVERIFY(exchange.retiredCount() <= 1);
    }

    void midiQueueKeepsOrderAndDropsWhenFull()
    {
        MidiQueue queue;
        QVERIFY(queue.push(MidiEvent{0x90, 60, 100, 0}));
        QVERIFY(queue.push(MidiEvent{0x80, 60, 0, 0}));
        MidiEvent event;
        QVERIFY(queue.pop(event));
        QCOMPARE(int(event.status), 0x90);
        QVERIFY(queue.pop(event));
        QCOMPARE(int(event.status), 0x80);
        QVERIFY(!queue.pop(event));

        int accepted = 0;
        while (queue.push(MidiEvent{0x90, 1, 1, 0})) ++accepted;
        QCOMPARE(accepted, MidiQueue::capacity());
    }

    void midiQueueWorksAcrossThreads()
    {
        MidiQueue queue;
        constexpr int kCount = 100000;
        std::thread producer([&] {
            for (int i = 0; i < kCount;) {
                if (queue.push(MidiEvent{0x90, static_cast<uint8_t>(i % 128), 1, i})) ++i;
            }
        });
        int expected = 0;
        MidiEvent event;
        while (expected < kCount) {
            if (queue.pop(event)) {
                QCOMPARE(event.sampleOffset, expected);
                ++expected;
            }
        }
        producer.join();
    }
};

QTEST_GUILESS_MAIN(TestGraphExchange)
#include "tst_graph_exchange.moc"
