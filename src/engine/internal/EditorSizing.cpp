#include "EditorSizing.h"

#include <cmath>
#include <cstdlib>

namespace gigchain::engine {

namespace {

// A plugin that rounds its size (Surge XT on Linux: two pixels taller than
// asked, every time) keeps it within this of the size asked: its rounding, not
// a size of its own.
constexpr int kRounding = 2;

} // namespace

KeptSize afterAskingSize(QSize natural, QSize shown, QSize kept)
{
    if (std::abs(kept.width() - shown.width()) <= kRounding && std::abs(kept.height() - shown.height()) <= kRounding) {
        // Its rounding: the window stays as it is (a pixel or two of the plugin
        // hidden or not covered). Following it would change the window, then fit it
        // again to the size asked, and so on for ever (#59: freezes every second).
        return KeptSize{.follow = false, .natural = natural};
    }
    if (shown.isEmpty() || kept.isEmpty()) return KeptSize{.follow = true, .natural = natural};
    // Fitted down into a room too small for it and kept bigger than asked: it does
    // not scale, so what it kept is its own size, as it is (scaling it up by the
    // fit would grow it at every fit, without end). Its window stays the room's
    // size, showing what fits: following it would only be fitted back again.
    const bool fittedDown = shown.width() < natural.width() - kRounding || shown.height() < natural.height() - kRounding;
    const bool refused = kept.width() > shown.width() + kRounding || kept.height() > shown.height() + kRounding;
    if (fittedDown && refused) return KeptSize{.follow = false, .natural = kept};
    // A size of its own (Analog Lab V keeping 814 x 886 when asked for 814 x 562):
    // that is its size from now on, in its own units (the size asked may have been
    // fitted down into the room), so the next fit asks for what it keeps.
    const double wide = static_cast<double>(natural.width()) / shown.width();
    const double high = static_cast<double>(natural.height()) / shown.height();
    return KeptSize{.follow = true,
                    .natural = QSize(static_cast<int>(std::lround(kept.width() * wide)), static_cast<int>(std::lround(kept.height() * high)))};
}

} // namespace gigchain::engine
