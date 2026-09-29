// Chord names as charts write them, to the notes they mean (for following
// what is played).
#include "gigchain/core/Chords.h"

#include <QtTest>

#include <initializer_list>
#include <numeric>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

int notes(std::initializer_list<int> pitchClasses)
{
    return std::accumulate(pitchClasses.begin(), pitchClasses.end(), 0, [](int bits, int pc) { return bits | (1 << pc); });
}

} // namespace

class TestChords : public QObject
{
    Q_OBJECT

private slots:
    void names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("root");
        QTest::addColumn<int>("family");
        QTest::addColumn<int>("bass");
        QTest::addColumn<int>("third");
        QTest::addColumn<int>("colour");
        QTest::newRow("C") << u"C"_s << 0 << notes({0, 4, 7}) << -1 << 4 << -1;
        QTest::newRow("Am") << u"Am"_s << 9 << notes({9, 0, 4}) << -1 << 3 << -1;
        QTest::newRow("G#m7") << u"G#m7"_s << 8 << notes({8, 11, 3, 6}) << -1 << 3 << -1;
        QTest::newRow("Bbmaj7/D") << u"Bbmaj7/D"_s << 10 << notes({10, 2, 5, 9}) << 2 << 4 << -1;
        QTest::newRow("CM7") << u"CM7"_s << 0 << notes({0, 4, 7, 11}) << -1 << 4 << -1;
        QTest::newRow("C-7") << u"C-7"_s << 0 << notes({0, 3, 7, 10}) << -1 << 3 << -1;
        QTest::newRow("Csus") << u"Csus"_s << 0 << notes({0, 5, 7}) << -1 << -1 << 5;
        QTest::newRow("Csus2") << u"Csus2"_s << 0 << notes({0, 2, 7}) << -1 << -1 << 2;
        QTest::newRow("D7sus4") << u"D7sus4"_s << 2 << notes({2, 7, 9, 0}) << -1 << -1 << 5;
        QTest::newRow("E5") << u"E5"_s << 4 << notes({4, 11}) << -1 << -1 << 7;
        QTest::newRow("F#m7b5") << u"F#m7b5"_s << 6 << notes({6, 9, 0, 4}) << -1 << 3 << -1;
        QTest::newRow("Adim7") << u"Adim7"_s << 9 << notes({9, 0, 3, 6}) << -1 << 3 << -1;
        QTest::newRow("Caug") << u"Caug"_s << 0 << notes({0, 4, 8}) << -1 << 4 << -1;
        QTest::newRow("Dadd9") << u"Dadd9"_s << 2 << notes({2, 6, 9, 4}) << -1 << 4 << -1;
        QTest::newRow("Cmaj7(no3)") << u"Cmaj7(no3)"_s << 0 << notes({0, 7, 11}) << -1 << -1 << 7;
        QTest::newRow("Ebmaj9") << u"Ebmaj9"_s << 3 << notes({3, 7, 10, 2, 5}) << -1 << 4 << -1;
        QTest::newRow("C/E") << u"C/E"_s << 0 << notes({0, 4, 7}) << 4 << 4 << -1;
        QTest::newRow("spaces") << u"  Am7  "_s << 9 << notes({9, 0, 4, 7}) << -1 << 3 << -1;
    }
    void names()
    {
        QFETCH(QString, name);
        QFETCH(int, root);
        QFETCH(int, family);
        QFETCH(int, bass);
        QFETCH(int, third);
        QFETCH(int, colour);
        const auto shape = parseChordName(name);
        if (!shape) QFAIL(qPrintable(u"not understood: "_s + name));
        QCOMPARE(shape->root, root);
        QCOMPARE(int(shape->family), family);
        QCOMPARE(shape->bass, bass);
        QCOMPARE(shape->third, third);
        QCOMPARE(shape->colour, colour);
    }

    void notChords_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("no chord") << u"N.C."_s;
        QTest::newRow("repeat") << u"x2"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("a word") << u"Hello"_s;
        QTest::newRow("lower case") << u"am"_s;
    }
    void notChords()
    {
        QFETCH(QString, name);
        QVERIFY(!parseChordName(name).has_value());
    }
};

QTEST_GUILESS_MAIN(TestChords)
#include "tst_chords.moc"
