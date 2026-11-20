#pragma once

#include <QSize>

namespace gigchain::engine {

// What the host takes from a plugin's editor keeping a size other than the
// one it was asked for: whether its window follows the size it kept, and the
// plugin's own size from now on (#59).
struct KeptSize
{
    bool follow = false;
    QSize natural;
};

// `natural`: the plugin's own size; `shown`: the size it was asked to be (its
// own size fitted into the room); `kept`: the size it is now.
[[nodiscard]] KeptSize afterAskingSize(QSize natural, QSize shown, QSize kept);

} // namespace gigchain::engine
