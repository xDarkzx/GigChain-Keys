// The splash's stage-crew lines.
#include "StageQuips.h"

#include <QSet>
#include <QtTest>

using namespace gigchain::ui;

class TestStageQuips : public QObject
{
    Q_OBJECT

private slots:
    void everyStepHasTenDifferentLines()
    {
        for (int i = 0; i < static_cast<int>(StageQuips::Step::Count); ++i) {
            const QStringList lines = StageQuips::lines(static_cast<StageQuips::Step>(i));
            QCOMPARE(lines.size(), 10);
            QCOMPARE(QSet<QString>(lines.begin(), lines.end()).size(), 10); // no repeats
            for (const QString& line : lines) QVERIFY(!line.trimmed().isEmpty());
        }
    }

    void oneLinePerStepForAWholeStart()
    {
        StageQuips quips;
        const QString first = quips.line(StageQuips::Step::Connecting);
        QVERIFY(StageQuips::lines(StageQuips::Step::Connecting).contains(first));
        for (int i = 0; i < 20; ++i) QCOMPARE(quips.line(StageQuips::Step::Connecting), first);
    }

    void startsDifferFromEachOther()
    {
        // Random per start: over many starts, more than one line shows up.
        QSet<QString> seen;
        for (int i = 0; i < 200; ++i) seen.insert(StageQuips().line(StageQuips::Step::Ready));
        QVERIFY(seen.size() > 1);
    }
};

QTEST_GUILESS_MAIN(TestStageQuips)
#include "tst_stage_quips.moc"
