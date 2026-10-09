#include "gigchain/core/KnobPickup.h"

#include <QtTest>

using namespace gigchain::core;

class TestKnobPickup : public QObject
{
    Q_OBJECT

private slots:
    // Straight is even; a gentle start leaves room at the bottom (half way
    // is a quarter); a quick start does most early. The ends are the ends.
    void curvesShapeTheKnobsTravel()
    {
        QCOMPARE(shapeKnob(0, 0), 0.0);
        QCOMPARE(shapeKnob(127, 0), 1.0);
        QVERIFY(qFuzzyCompare(shapeKnob(64, 0), 64 / 127.0));
        for (const int curve : {1, 2}) {
            QCOMPARE(shapeKnob(0, curve), 0.0);
            QCOMPARE(shapeKnob(127, curve), 1.0);
        }
        QVERIFY(qFuzzyCompare(shapeKnob(64, 1), (64 / 127.0) * (64 / 127.0)));
        QVERIFY(shapeKnob(64, 1) < shapeKnob(64, 0));
        QVERIFY(shapeKnob(64, 2) > shapeKnob(64, 0));
        QCOMPARE(shapeKnob(-5, 0), 0.0); // out of range: held to the ends
        QCOMPARE(shapeKnob(200, 1), 1.0);
        QCOMPARE(shapeKnob(64, 9), shapeKnob(64, 0)); // an unknown curve: straight
    }

    // A knob away from the setting moves nothing until it reaches it, or
    // passes it between two moves; then it follows. Not knowing where the
    // setting is, it takes over at once.
    void aKnobTakesOverOnlyOnceItReachesTheSetting()
    {
        KnobPickup below;
        QVERIFY(!below.take(0.1, 0.7));
        QVERIFY(!below.take(0.5, 0.7));
        QVERIFY(below.take(0.9, 0.7)); // passed it
        QVERIFY(below.take(0.1, 0.7)); // then it follows, wherever it goes

        KnobPickup above;
        QVERIFY(!above.take(1.0, 0.3));
        QVERIFY(above.take(0.2, 0.3)); // passed it coming down

        KnobPickup close;
        QVERIFY(close.take(0.71, 0.7)); // near enough at once

        KnobPickup unknown;
        QVERIFY(unknown.take(0.0, -1.0));
    }
};

QTEST_GUILESS_MAIN(TestKnobPickup)
#include "tst_knob_pickup.moc"
