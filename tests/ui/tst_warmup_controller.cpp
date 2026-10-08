// The Practice tab's warm-up: today's routine (each exercise right hand,
// left hand, both), a run played and scored, the tempo moving up after
// three clean both-hands runs, kept for next time.
#include "DocumentController.h"
#include "PracticeController.h"
#include "SpyEngine.h"
#include "WarmupController.h"

#include "gigchain/core/Warmup.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestWarmupController : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;
    std::unique_ptr<PracticeController> m_practice;
    std::unique_ptr<WarmupController> m_warmup;
    int64_t m_now = 1'000'000'000; // the clock the controller reads (ns)

    void makeWarmup()
    {
        m_warmup = std::make_unique<WarmupController>(*m_engine, *m_practice, *m_settings);
        m_warmup->setClock([this] { return m_now; });
    }

    // Plays the run loaded now: every note at its beat (`lateMs` after it),
    // the clock and the falling notes moving together to the end.
    void playTheRun(double lateMs = 0.0)
    {
        const double tempo = m_practice->tempo();
        const double msPerBeat = 60000.0 / tempo;
        const int64_t start = m_now; // the run's beat 0 of the count-in
        for (const QVariant& n : m_practice->notes()) {
            const QVariantMap note = n.toMap();
            const double ms = (note.value(u"start"_s).toDouble() * msPerBeat) + lateMs;
            m_engine->keyPresses.push_back(engine::KeyPress{.note = note.value(u"pitch"_s).toInt(), .velocity = 100,
                                                            .timeNs = start + static_cast<int64_t>(ms * 1e6)});
        }
        for (int i = 0; i < 10000 && m_practice->playing(); ++i) {
            m_now += 20'000'000;
            m_practice->advance(20.0);
        }
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
        m_practice = std::make_unique<PracticeController>(*m_engine, *m_doc);
        makeWarmup();
    }

    void cleanup()
    {
        m_warmup.reset();
        m_practice.reset();
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    // The warm-up starts at Beginner with its exercises listed; switched
    // off, the song's notes come back.
    void theWarmupListsItsLevel()
    {
        m_warmup->setActive(true);
        QCOMPARE(m_warmup->level(), 0);
        QCOMPARE(m_warmup->unlockedLevel(), 0);
        const QVariantList exercises = m_warmup->exercises();
        QCOMPARE(exercises.size(), 4);
        QCOMPARE(exercises.at(1).toMap().value(u"id"_s).toString(), u"b-five-c"_s);
        QCOMPARE(exercises.at(1).toMap().value(u"tempo"_s).toDouble(), 60.0);
        QVERIFY(m_warmup->routineMinutes() >= 2);
        m_warmup->setLevel(2); // not open yet: stays
        QCOMPARE(m_warmup->level(), 0);
        m_warmup->startExercise(1, 0);
        QVERIFY(m_practice->exercise());
        m_warmup->setActive(false);
        QVERIFY(!m_practice->exercise());
    }

    // A run of C-D-E-F-G and back with the right hand: its falling notes
    // carry their fingers; played perfectly, it scores three stars.
    void aRunIsPlayedAndScored()
    {
        m_warmup->setActive(true);
        m_warmup->startExercise(1, static_cast<int>(core::WarmupHands::Right));
        QVERIFY(m_warmup->playing());
        QCOMPARE(m_practice->tempo(), 60.0);
        QCOMPARE(m_practice->notes().size(), 9);
        QCOMPARE(m_practice->notes().at(0).toMap().value(u"finger"_s).toInt(), 1);
        QCOMPARE(m_practice->notes().at(4).toMap().value(u"finger"_s).toInt(), 5);
        playTheRun();
        QVERIFY(!m_warmup->playing());
        const QVariantMap result = m_warmup->result();
        QCOMPARE(result.value(u"stars"_s).toInt(), 3);
        QCOMPARE(result.value(u"right"_s).toInt(), 9);
        QVERIFY(result.value(u"clean"_s).toBool());
        QVERIFY(result.value(u"timingMs"_s).toDouble() < 5.0);

        // Played 120 ms late: right notes, not clean, and it says so.
        m_warmup->again();
        playTheRun(120.0);
        QVERIFY(!m_warmup->result().value(u"clean"_s).toBool());
        QVERIFY2(m_warmup->result().value(u"tip"_s).toString().contains(u"behind"_s), qPrintable(m_warmup->result().value(u"tip"_s).toString()));
    }

    // Three clean both-hands runs: 5 BPM faster, kept for next time.
    void cleanRunsMoveTheTempoUpAndAreKept()
    {
        m_warmup->setActive(true);
        m_warmup->startExercise(1, static_cast<int>(core::WarmupHands::Both));
        playTheRun();
        QCOMPARE(m_warmup->exercises().at(1).toMap().value(u"cleanRuns"_s).toInt(), 1);
        m_warmup->again();
        playTheRun();
        m_warmup->again();
        playTheRun();
        QVERIFY(m_warmup->result().value(u"tempoUp"_s).toBool());
        QCOMPARE(m_warmup->exercises().at(1).toMap().value(u"tempo"_s).toDouble(), 65.0);
        m_warmup->again();
        QCOMPARE(m_practice->tempo(), 65.0);

        makeWarmup(); // the next day
        m_warmup->setActive(true);
        QCOMPARE(m_warmup->exercises().at(1).toMap().value(u"tempo"_s).toDouble(), 65.0);
        QCOMPARE(m_warmup->exercises().at(1).toMap().value(u"stars"_s).toInt(), 3);
    }

    // Today's warm-up: each exercise of the level right hand, left hand,
    // both; Next goes on through them, then it is done.
    void todaysWarmupGoesThroughEveryExercise()
    {
        m_warmup->setActive(true);
        m_warmup->startRoutine();
        QVERIFY(m_warmup->routine());
        QCOMPARE(m_warmup->routineSteps(), 12);
        QCOMPARE(m_warmup->exercise(), 0);
        QCOMPARE(m_warmup->hands(), static_cast<int>(core::WarmupHands::Right));
        m_warmup->next();
        QCOMPARE(m_warmup->hands(), static_cast<int>(core::WarmupHands::Left));
        m_warmup->next();
        QCOMPARE(m_warmup->hands(), static_cast<int>(core::WarmupHands::Both));
        m_warmup->next();
        QCOMPARE(m_warmup->exercise(), 1);
        QCOMPARE(m_warmup->routineStep(), 3);
        for (int i = 0; i < 8; ++i) m_warmup->next();
        QCOMPARE(m_warmup->routineStep(), 11);
        m_warmup->next(); // the last: done
        QVERIFY(!m_warmup->routine());
        QVERIFY(m_warmup->finishedToday());
    }
};

QTEST_GUILESS_MAIN(TestWarmupController)
#include "tst_warmup_controller.moc"
