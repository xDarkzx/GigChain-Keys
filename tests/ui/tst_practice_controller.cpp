// The Practice tab's player: the current song's chart as falling notes, in
// time with its tempo; Listen plays them through the patch, Wait for me
// waits for the player's keys, a section loops, the speed slows it all.
#include "DocumentController.h"
#include "PracticeController.h"
#include "SpyEngine.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestPracticeController : public QObject
{
    Q_OBJECT

    // The notes sent to the engine since the last look: (note, velocity).
    std::vector<std::pair<int, int>> sent()
    {
        std::vector<std::pair<int, int>> out(m_engine->notes.size());
        std::ranges::transform(m_engine->notes, out.begin(), [](const auto& n) { return std::pair{n.at(1), n.at(2)}; });
        m_engine->notes.clear();
        return out;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
        m_engine = std::make_unique<test::SpyEngine>();
        m_doc = std::make_unique<DocumentController>(*m_engine, *m_settings);
        m_doc->newSetlist();
        QVERIFY(m_doc->addSong());
        // 120 BPM in 4/4: a beat is half a second.
        QVERIFY(m_doc->setSongChart(0, u"{comment: Verse}\n[C]a [F]b\n{comment: Chorus}\n[G]c [C]d\n"_s));
        QVERIFY(m_doc->setSectionBars(0, 2));
        QVERIFY(m_doc->setSectionBars(1, 2));
        m_practice = std::make_unique<PracticeController>(*m_engine, *m_doc);
    }

    void cleanup()
    {
        m_practice.reset();
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    // The chart as notes: each chord's bass and right hand, in beats.
    void theSongBecomesFallingNotes()
    {
        QCOMPARE(m_practice->chords().size(), 4);
        QCOMPARE(m_practice->chords().at(0).toMap().value(u"name"_s).toString(), u"C"_s);
        QCOMPARE(m_practice->chords().at(0).toMap().value(u"start"_s).toDouble(), 4.0); // after the count-in
        QCOMPARE(m_practice->chords().at(2).toMap().value(u"start"_s).toDouble(), 12.0);
        QCOMPARE(m_practice->notes().size(), 16); // 4 chords: a bass and three notes each
        QCOMPARE(m_practice->length(), 20.0);
        QCOMPARE(m_practice->sections().size(), 2);
        QCOMPARE(m_practice->nextChord(), u"C"_s);
        // The chart changed: the notes follow.
        QVERIFY(m_doc->setSongChart(0, u"[Am]x [Dm]y [E]z"_s));
        QCOMPARE(m_practice->chords().size(), 3);
    }

    // Listen: the notes are played through the patch as they land, and let go.
    void listenPlaysTheNotes()
    {
        m_practice->setMode(PracticeController::Listen);
        m_practice->play();
        m_practice->advance(1900); // the count-in (2 s), not yet
        QVERIFY(sent().empty());
        m_practice->advance(200); // into the first chord
        const auto first = sent();
        QCOMPARE(first.size(), std::size_t{4});
        QVERIFY(std::ranges::all_of(first, [](const auto& n) { return n.second > 0; }));
        QCOMPARE(m_practice->nowChord(), u"C"_s);
        QCOMPARE(m_practice->nextChord(), u"F"_s);
        QVERIFY(m_practice->targetNotes().contains(60));
        m_practice->advance(2000); // the F: C's notes let go, F's played
        const auto change = sent();
        QVERIFY(std::ranges::any_of(change, [](const auto& n) { return n.first == 36 && n.second == 0; })); // C2 up
        QVERIFY(std::ranges::any_of(change, [](const auto& n) { return n.first == 41 && n.second > 0; }));  // F2 down
        // Paused: every note let go.
        m_practice->pause();
        const auto paused = sent();
        QVERIFY(!paused.empty());
        QVERIFY(std::ranges::all_of(paused, [](const auto& n) { return n.second == 0; }));
    }

    // Play along: nothing is played for the player.
    void playAlongPlaysNothing()
    {
        m_practice->setMode(PracticeController::PlayAlong);
        m_practice->play();
        m_practice->advance(3000);
        QVERIFY(sent().empty());
        QVERIFY(m_practice->position() > 4.0);
    }

    // Wait for me: the chord waits until its notes are held.
    void waitForMeWaitsForTheKeys()
    {
        m_practice->setMode(PracticeController::WaitForMe);
        m_practice->play();
        m_practice->advance(5000);
        QCOMPARE(m_practice->position(), 4.0); // at the C, waiting
        QVERIFY(m_practice->waiting());
        // Only part of it: still waiting.
        m_engine->keyboard.velocity.at(60) = 80;
        m_practice->advance(500);
        QCOMPARE(m_practice->position(), 4.0);
        // All of it: it goes on.
        for (const QVariant& note : m_practice->targetNotes()) m_engine->keyboard.velocity.at(static_cast<std::size_t>(note.toInt())) = 80;
        m_practice->advance(500);
        QVERIFY(!m_practice->waiting());
        QVERIFY(m_practice->position() > 4.0);
        QVERIFY(sent().empty()); // nothing played for the player
    }

    // Slower: the beats go by at the speed asked for.
    void theSpeedSlowsItDown()
    {
        m_practice->setMode(PracticeController::PlayAlong);
        m_practice->setSpeed(0.5);
        m_practice->play();
        m_practice->advance(1000); // a second at half speed: one beat
        QCOMPARE(m_practice->position(), 1.0);
        m_practice->setSpeed(0.1); // too slow: 25 % at least
        QCOMPARE(m_practice->speed(), 0.25);
    }

    // A section looped: from its end back to its start, round and round.
    void aSectionLoops()
    {
        m_practice->setMode(PracticeController::PlayAlong);
        m_practice->setLoopSection(1); // the chorus: beats 12 to 20
        QCOMPARE(m_practice->position(), 12.0);
        m_practice->play();
        m_practice->advance(4500); // 9 beats: past the end, round again
        QCOMPARE(m_practice->position(), 13.0);
        QVERIFY(m_practice->playing());
        // Stop: back to the loop's start.
        m_practice->stop();
        QCOMPARE(m_practice->position(), 12.0);
        // The whole song: it stops at the end.
        m_practice->setLoopSection(-1);
        m_practice->play();
        m_practice->advance(20000);
        QVERIFY(!m_practice->playing());
    }

    // A song without chords: nothing to practise, said on the screen.
    void aSongWithoutChordsHasNothingToPlay()
    {
        QVERIFY(m_doc->setSongChart(0, u"only words"_s));
        QVERIFY(m_practice->chords().isEmpty());
        m_practice->play();
        QVERIFY(!m_practice->playing());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;
    std::unique_ptr<PracticeController> m_practice;
};

QTEST_GUILESS_MAIN(TestPracticeController)
#include "tst_practice_controller.moc"
