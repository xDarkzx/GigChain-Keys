// A plugin's window and its room settle on a size: no endless resizing when the
// plugin keeps a size a little or a lot different from the one asked (#59).
#include "EditorSizing.h"

#include <QtTest>

#include <algorithm>
#include <functional>

using namespace gigchain::engine;

namespace {

// The host's loop, as Vst3Editor and PluginEditorHost run it: each fit first
// sizes the window from the plugin's own size fitted into the room (never
// bigger, same shape), asks the plugin for that size, and the window then
// follows what it kept; any change of the window's size changes its room,
// which fits again. How many rounds until a fit changes nothing (0: never
// settled in `most`).
struct Outcome
{
    int rounds = 0;
    QSize window;
    QSize plugin;
};
Outcome settle(QSize own, QSize room, const std::function<QSize(QSize)>& keeps, int most = 50)
{
    QSize natural = own;
    QSize window;
    QSize plugin;
    for (int round = 1; round <= most; ++round) {
        bool changed = false;
        const auto resize = [&window, &changed](QSize size) {
            changed = changed || size != window;
            window = size;
        };
        const double scale = std::min({1.0, double(room.width()) / natural.width(), double(room.height()) / natural.height()});
        const QSize shown(int(natural.width() * scale), int(natural.height() * scale));
        resize(shown); // (the fitter, from the plugin's own size)
        plugin = keeps(shown);
        if (plugin != shown) {
            const KeptSize after = afterAskingSize(natural, shown, plugin);
            natural = after.natural;
            if (after.follow) resize(plugin); // (the window follows what it kept)
        }
        if (!changed) return Outcome{.rounds = round, .window = window, .plugin = plugin};
    }
    return Outcome{.rounds = 0, .window = window, .plugin = plugin};
}

} // namespace

class TestEditorSizing : public QObject
{
    Q_OBJECT

private slots:
    // Surge XT on Linux: two pixels taller than asked, every time (the report's 686 x 429 for 686 x 427).
    void aPluginRoundingItsSizeSettles()
    {
        const auto twoTaller = [](QSize asked) { return QSize(asked.width(), asked.height() + 2); };
        const Outcome big = settle(QSize(686, 427), QSize(2000, 1200), twoTaller);
        QVERIFY2(big.rounds > 0 && big.rounds <= 3, qPrintable(QString::number(big.rounds)));
        const Outcome small = settle(QSize(1598, 996), QSize(900, 600), twoTaller); // fitted down
        QVERIFY2(small.rounds > 0 && small.rounds <= 3, qPrintable(QString::number(small.rounds)));
        QVERIFY(std::abs(small.window.height() - small.plugin.height()) <= 2);
    }

    // Analog Lab V: asked 814 x 562, it stays 814 x 886 (a size of its own): the window shows all of it.
    void aPluginKeepingItsOwnSizeSettles()
    {
        const auto ownSize = [](QSize) { return QSize(814, 886); };
        const Outcome outcome = settle(QSize(814, 562), QSize(2000, 1200), ownSize);
        QVERIFY2(outcome.rounds > 0 && outcome.rounds <= 3, qPrintable(QString::number(outcome.rounds)));
        QCOMPARE(outcome.window, QSize(814, 886));
    }

    // A plugin that takes the size it is given: one round, nothing changed.
    void aPluginTakingItsSizeIsLeftAlone()
    {
        const Outcome outcome = settle(QSize(800, 500), QSize(2000, 1200), [](QSize asked) { return asked; });
        QCOMPARE(outcome.rounds, 2); // (the first sets the window, the second finds it unchanged)
        QCOMPARE(outcome.window, QSize(800, 500));
        const KeptSize same = afterAskingSize(QSize(800, 500), QSize(800, 500), QSize(801, 499));
        QVERIFY(!same.follow); // (a pixel off: its rounding, not a size of its own)
        QCOMPARE(same.natural, QSize(800, 500));
    }
};

QTEST_GUILESS_MAIN(TestEditorSizing)
#include "tst_editor_sizing.moc"
