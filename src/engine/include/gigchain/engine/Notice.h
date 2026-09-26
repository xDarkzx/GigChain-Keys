#pragma once

#include <QString>

#include <utility>

namespace gigchain::engine {

// Something the engine tells the user, and how much it matters: a keyboard
// plugged in is news (Info); falling back to system audio still plays
// (Warning); a plugin that did not load, or no sound at all, is an Error.
// Every notice is also logged where it is raised.
struct Notice
{
    enum class Level { Info, Warning, Error };

    Level level = Level::Info;
    QString text;

    static Notice info(QString text) { return {.level = Level::Info, .text = std::move(text)}; }
    static Notice warning(QString text) { return {.level = Level::Warning, .text = std::move(text)}; }
    static Notice error(QString text) { return {.level = Level::Error, .text = std::move(text)}; }
};

} // namespace gigchain::engine
