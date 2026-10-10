#include "ExternalMidiOut.h"
#include "HardwareOut.h"
#include "HardwareSender.h"
#include "MidiEvent.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>

using namespace gigchain;
using namespace Qt::StringLiterals;

class TestHardwareSender : public QObject
{
    Q_OBJECT

private slots:
    // The sender's own thread takes a channel's notes and sends them; an
    // output that is not there is said once, the notes after it are counted
    // as lost, and the sender stops cleanly when it goes.
    void aMissingSynthIsSaidOnceAndTheSenderStops()
    {
        engine::ExternalMidiOut midi;
        auto out = std::make_shared<engine::HardwareOut>(u"No such synth"_s, 1);
        {
            engine::HardwareSender sender(midi);
            sender.setOuts({out});
            const std::array<engine::MidiEvent, 1> note{engine::MidiEvent{.status = 0x90, .data1 = 60, .data2 = 100}};
            out->push(note, true);
            // (Gathered: QTRY checks its condition once more at the end, and taking empties them.)
            std::vector<QString> problems;
            const auto gather = [&] {
                std::ranges::move(sender.takeProblems(), std::back_inserter(problems));
                return !problems.empty();
            };
            QTRY_VERIFY_WITH_TIMEOUT(gather(), 5000);
            QCOMPARE(problems.size(), std::size_t{1});
            QVERIFY2(problems.front().contains(u"No such synth"_s), qPrintable(problems.front()));

            const std::array<engine::MidiEvent, 1> next{engine::MidiEvent{.status = 0x90, .data1 = 62, .data2 = 100}};
            out->push(next, true);
            uint64_t dropped = 0;
            const auto count = [&] {
                dropped += sender.takeDropped();
                return dropped == 1;
            };
            QTRY_VERIFY_WITH_TIMEOUT(count(), 5000);
            QVERIFY(sender.takeProblems().empty()); // not said again while it rests
        } // the sender's thread stops and joins here (the test would hang otherwise)
    }

    // A sender that never had a synth to serve starts no thread and goes at once.
    void aSenderWithNothingToServeGoesAtOnce()
    {
        engine::ExternalMidiOut midi;
        engine::HardwareSender sender(midi);
        sender.setOuts({});
        QVERIFY(sender.takeProblems().empty());
        QCOMPARE(sender.takeDropped(), uint64_t{0});
    }
};

QTEST_GUILESS_MAIN(TestHardwareSender)
#include "tst_hardware_sender.moc"
