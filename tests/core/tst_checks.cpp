#include "gigchain/core/Checks.h"

#include <QtTest>

#include <memory>

using namespace Qt::StringLiterals;

class TestChecks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { gigchain::core::checks::setFatal(false); } // test the release-build fallback
    void cleanupTestCase() { gigchain::core::checks::setFatal(true); }

    void aCheckThatHoldsDoesNothing()
    {
        int fallbacks = 0;
        const int value = 3;
        GC_IF_FAILED(value == 3) { ++fallbacks; }
        QCOMPARE(fallbacks, 0);
    }

    void aFailedCheckIsLoggedWithWhereAndTakesTheFallback()
    {
        const std::unique_ptr<int> missing;
        int fallbacks = 0;
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(u"Check failed: missing at .*tst_checks\\.cpp : \\d+"_s));
        GC_IF_FAILED(missing) { ++fallbacks; }
        QCOMPARE(fallbacks, 1);
    }

    void theConditionIsEvaluatedOnce()
    {
        int calls = 0;
        const auto holds = [&calls] { ++calls; return true; };
        GC_IF_FAILED(holds()) {}
        QCOMPARE(calls, 1);
    }

    void threadRulesHoldOnTheirThreads()
    {
        GC_ONLY_MAIN_THREAD(); // the test runs on the main thread
        bool ranElsewhere = false;
        std::unique_ptr<QThread> worker(QThread::create([&ranElsewhere] {
            GC_ONLY_AUDIO_THREAD(); // any thread but the main one
            ranElsewhere = true;
        }));
        worker->start();
        QVERIFY(worker->wait(5000));
        QVERIFY(ranElsewhere);
    }
};

QTEST_GUILESS_MAIN(TestChecks)
#include "tst_checks.moc"
