// "Learn this keyboard": every control learned (in the setlist and Settings)
// listed once by what it does, and taught one by one from another keyboard.
#include "KeyboardTeaching.h"

#include "gigchain/core/Model.h"

#include <QtTest>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

constexpr engine::MidiTrigger cc(uint8_t channel, uint8_t number)
{
    return engine::MidiTrigger{.kind = engine::MidiTrigger::ControlChange, .channel = channel, .number = number};
}

// Two songs: both turn a filter with CC 74; one also a reverb with CC 91.
// The mixer's master fader on CC 7, the looper's Record on a pad (note 36, channel 10).
core::Setlist learnedSetlist()
{
    core::Setlist setlist;
    for (const QString& name : {u"Morning Light"_s, u"Blue Harbour"_s}) {
        core::Song song;
        song.name = name;
        core::Patch patch = core::makePatch(u"Verse"_s);
        core::Channel channel = core::makeChannel(u"Synth"_s);
        channel.mappings.push_back(core::ControlMapping{.midiChannel = 1, .controller = 74, .parameterName = u"Cutoff"_s});
        if (name == u"Blue Harbour"_s) {
            channel.mappings.push_back(core::ControlMapping{.midiChannel = 0, .controller = 91, .parameterName = u"Reverb"_s});
        }
        patch.channels.push_back(channel);
        song.patches.push_back(patch);
        setlist.songs.push_back(song);
    }
    setlist.mixerControls.master = core::LearnedControl{.kind = 0xB0, .channel = 1, .number = 7};
    setlist.loopControls.buttons.at(core::LoopControls::Record) = core::LearnedControl{.kind = 0x90, .channel = 10, .number = 36};
    return setlist;
}

} // namespace

class TestKeyboardTeaching : public QObject
{
    Q_OBJECT

private slots:
    // Each control once (the cutoff knob of both songs is one knob), said by
    // what it does: the buttons of Settings, then the setlist's.
    void everyLearnedControlIsListedOnceByWhatItDoes()
    {
        engine::ControlTriggers buttons{};
        buttons.at(static_cast<std::size_t>(engine::ControlAction::NextSong)) = cc(0, 64);
        const std::vector<TeachableControl> controls = controlsInUse(learnedSetlist(), buttons);
        QCOMPARE(controls.size(), std::size_t{5});
        const auto find = [&](engine::MidiTrigger t) {
            return std::ranges::find_if(controls, [&](const TeachableControl& c) { return c.learned == t; });
        };
        QVERIFY(find(cc(0, 64))->label.contains(u"Next song"_s));
        QVERIFY(find(cc(0, 7))->label.contains(u"Master"_s));
        const auto cutoff = find(cc(0, 74));
        QVERIFY(cutoff != controls.end());
        QVERIFY2(cutoff->label.contains(u"Cutoff"_s) && cutoff->label.contains(u"2 songs"_s), qPrintable(cutoff->label));
        QVERIFY(find(cc(0, 91)) != controls.end()); // learned for any channel: as channel 1
        const auto record = find(engine::MidiTrigger{.kind = engine::MidiTrigger::Note, .channel = 9, .number = 36});
        QVERIFY(record != controls.end());
        QVERIFY(record->label.contains(u"Record"_s));
    }

    // Taught one by one: each control moved on the other keyboard stands for
    // the one asked; still moving the last one is not the next; a skipped
    // one is left as it is; the same control is not taken twice.
    void eachControlMovedStandsForTheOneAsked()
    {
        KeyboardTeaching teaching({{.learned = cc(0, 74), .label = u"Cutoff"_s},
                                   {.learned = cc(0, 91), .label = u"Reverb"_s},
                                   {.learned = cc(0, 7), .label = u"Master"_s}});
        QCOMPARE(teaching.current()->label, u"Cutoff"_s);
        QVERIFY(teaching.take(cc(1, 21)));          // the backup board's first knob
        QCOMPARE(teaching.current()->label, u"Reverb"_s);
        QVERIFY(!teaching.take(cc(1, 21)));         // still turning it: not the next one
        QVERIFY(teaching.take(cc(1, 22)));
        teaching.skip();                             // no master fader on this board
        QVERIFY(teaching.done());
        const engine::KeyboardRig rig = teaching.rig();
        QCOMPARE(rig.size(), std::size_t{2});
        QVERIFY((rig.at(0) == engine::RigEntry{.from = cc(1, 21), .to = cc(0, 74)}));
        QVERIFY((rig.at(1) == engine::RigEntry{.from = cc(1, 22), .to = cc(0, 91)}));

        teaching.back(); // back to the master: not taught
        QCOMPARE(teaching.current()->label, u"Master"_s);
        QVERIFY(!teaching.take(cc(1, 21))); // already the cutoff's
    }

    // A control the board sends just as learned needs nothing.
    void theSameControlNeedsNothing()
    {
        KeyboardTeaching teaching({{.learned = cc(0, 74), .label = u"Cutoff"_s}});
        QVERIFY(teaching.take(cc(0, 74)));
        QVERIFY(teaching.done());
        QVERIFY(teaching.rig().empty());
    }
};

QTEST_GUILESS_MAIN(TestKeyboardTeaching)
#include "tst_keyboard_teaching.moc"
