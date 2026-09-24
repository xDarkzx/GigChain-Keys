#include "gigchain/core/Navigation.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

// Song 0: 2 patches, song 1: 1 patch, song 2: 3 patches.
Setlist threeSongs()
{
    Setlist setlist;
    const int patchCounts[] = {2, 1, 3};
    for (const int count : patchCounts) {
        Song song = makeSong(u"Song"_s);
        for (int i = 1; i < count; ++i) {
            song.patches.push_back(makePatch(u"Patch"_s));
        }
        setlist.songs.push_back(song);
    }
    return setlist;
}

} // namespace

class TestNavigation : public QObject
{
    Q_OBJECT

private slots:
    void emptySetlistHasNoPatch()
    {
        const Setlist empty;
        QVERIFY(!firstPatch(empty).isValid());
        QVERIFY(!nextPatch(empty, Cursor{}).isValid());
        QVERIFY(patchAt(empty, Cursor{0, 0}) == nullptr);
    }

    void nextPatchWalksAcrossSongsAndStopsAtEnd()
    {
        const Setlist s = threeSongs();
        Cursor c = firstPatch(s);
        QVERIFY(c == (Cursor{0, 0}));
        const Cursor expected[] = {{0, 1}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {2, 2}};
        for (const Cursor& e : expected) {
            c = nextPatch(s, c);
            QVERIFY(c == e);
        }
    }

    void previousPatchWalksBackAndStopsAtStart()
    {
        const Setlist s = threeSongs();
        Cursor c{2, 0};
        const Cursor expected[] = {{1, 0}, {0, 1}, {0, 0}, {0, 0}};
        for (const Cursor& e : expected) {
            c = previousPatch(s, c);
            QVERIFY(c == e);
        }
    }

    void songJumpsLandOnFirstPatch()
    {
        const Setlist s = threeSongs();
        QVERIFY(nextSong(s, Cursor{0, 1}) == (Cursor{1, 0}));
        QVERIFY(previousSong(s, Cursor{2, 2}) == (Cursor{1, 0}));
        QVERIFY(nextSong(s, Cursor{2, 1}) == (Cursor{2, 1}));
        QVERIFY(previousSong(s, Cursor{0, 1}) == (Cursor{0, 1}));
    }

    void clampRepairsStaleCursors()
    {
        const Setlist s = threeSongs();
        QVERIFY(clampCursor(s, Cursor{0, 5}) == (Cursor{0, 1}));
        QVERIFY(clampCursor(s, Cursor{9, 0}) == (Cursor{2, 0}));
        QVERIFY(clampCursor(s, Cursor{-1, -1}) == (Cursor{0, 0}));
        QVERIFY(clampCursor(s, Cursor{1, 0}) == (Cursor{1, 0}));
    }

    void findsPatchesById()
    {
        const Setlist s = threeSongs();
        const PatchId id = s.songs[2].patches[1].id;
        const auto found = findPatch(s, id);
        QVERIFY(found.has_value());
        QVERIFY(*found == (Cursor{2, 1}));
        QVERIFY(!findPatch(s, PatchId::generate()).has_value());
        QCOMPARE(patchAt(s, *found)->id, id);
    }

    void skipsSongsWithoutPatches()
    {
        Setlist s = threeSongs();
        s.songs[1].patches.clear();
        QVERIFY(nextPatch(s, Cursor{0, 1}) == (Cursor{2, 0}));
        QVERIFY(previousPatch(s, Cursor{2, 0}) == (Cursor{0, 1}));
    }
};

QTEST_GUILESS_MAIN(TestNavigation)
#include "tst_navigation.moc"
