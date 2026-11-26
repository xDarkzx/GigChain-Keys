// The screen stays on while the sound runs (#92): a MIDI keyboard is no
// activity to the system, so without it the screen would go dark mid-set.
#include "StayAwake.h"

#include "SpyEngine.h"

#include <QRegularExpression>
#include <QtTest>

#include <chrono>
#include <vector>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestStayAwake : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_engine = std::make_unique<test::SpyEngine>();
        m_asked.clear();
        m_refuse = false;
        m_refuseRelease = false;
    }

    void theScreenIsKeptOnWhileTheSoundRuns()
    {
        StayAwake awake(*m_engine, ask());
        awake.poll();
        QVERIFY(m_asked.empty()); // no sound yet: nothing asked
        QVERIFY(!awake.holding());

        m_engine->clock.running = true;
        awake.poll();
        QCOMPARE(m_asked, (std::vector<bool>{true}));
        QVERIFY(awake.holding());
        awake.poll();
        awake.poll();
        QCOMPARE(m_asked.size(), 1U); // held: not asked again each poll

        m_engine->clock.running = false; // the device lost, or the app between devices
        awake.poll();
        QCOMPARE(m_asked, (std::vector<bool>{true, false}));
        QVERIFY(!awake.holding());
    }

    void switchedOffTheScreenSleepsAsUsual()
    {
        StayAwake awake(*m_engine, ask());
        m_engine->clock.running = true;
        awake.poll();
        QVERIFY(awake.holding());

        awake.setEnabled(false); // let go at once, not at the next poll
        QCOMPARE(m_asked, (std::vector<bool>{true, false}));
        QVERIFY(!awake.holding());
        awake.poll();
        QCOMPARE(m_asked.size(), 2U);

        awake.setEnabled(true); // the sound still runs: held again at once
        QCOMPARE(m_asked, (std::vector<bool>{true, false, true}));
        QVERIFY(awake.holding());
    }

    void startedSwitchedOffNothingIsAsked()
    {
        StayAwake awake(*m_engine, ask(), false);
        m_engine->clock.running = true;
        awake.poll();
        QVERIFY(m_asked.empty());
        QVERIFY(!awake.enabled());
    }

    // The system said no: said once, with why, not at every poll; asked again
    // when the sound starts again.
    void aRefusalIsLoggedOnceAndAskedAgainNextTime()
    {
        StayAwake awake(*m_engine, ask());
        m_refuse = true;
        m_engine->clock.running = true;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Could not keep the screen on.*no screen saver here"_s));
        awake.poll();
        awake.poll();
        awake.poll();
        QCOMPARE(m_asked, (std::vector<bool>{true}));
        QVERIFY(!awake.holding());

        m_engine->clock.running = false;
        awake.poll();
        QCOMPARE(m_asked.size(), 1U); // nothing held: nothing to let go

        m_refuse = false;
        m_engine->clock.running = true;
        awake.poll();
        QCOMPARE(m_asked, (std::vector<bool>{true, true}));
        QVERIFY(awake.holding());
    }

    // Letting go failed (Windows kept the request): still held, said once,
    // asked again a while later (not at every poll), and let go when the
    // system takes it.
    void aFailedReleaseIsTriedAgain()
    {
        StayAwake awake(*m_engine, ask(), true, std::chrono::milliseconds(50));
        m_engine->clock.running = true;
        awake.poll();
        QVERIFY(awake.holding());

        m_refuseRelease = true;
        m_engine->clock.running = false;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"still holding"_s));
        awake.poll();
        awake.poll(); // (too soon to ask again)
        QCOMPARE(m_asked, (std::vector<bool>{true, false}));
        QVERIFY(awake.holding()); // not let go: it says so
        QTest::qWait(80);
        awake.poll(); // asked again, quietly
        QCOMPARE(m_asked, (std::vector<bool>{true, false, false}));
        QVERIFY(awake.holding());

        m_refuseRelease = false;
        QTest::qWait(80);
        awake.poll();
        QCOMPARE(m_asked, (std::vector<bool>{true, false, false, false}));
        QVERIFY(!awake.holding());
        awake.poll();
        QCOMPARE(m_asked.size(), 4U); // let go: nothing more to ask
    }

private:
    StayAwake::Ask ask()
    {
        return [this](bool on) -> core::Result<void> {
            m_asked.push_back(on);
            if (on && m_refuse) return core::fail(core::ErrorCode::SystemRefused, u"no screen saver here"_s);
            if (!on && m_refuseRelease) return core::fail(core::ErrorCode::SystemRefused, u"still holding"_s);
            return {};
        };
    }

    std::unique_ptr<test::SpyEngine> m_engine;
    std::vector<bool> m_asked;
    bool m_refuse = false;
    bool m_refuseRelease = false;
};

QTEST_GUILESS_MAIN(TestStayAwake)
#include "tst_stay_awake.moc"
