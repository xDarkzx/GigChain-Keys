#pragma once

#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace gigchain::ui {

// What the splash says while starting: a stage-crew line for each start-up
// step instead of what the app is technically doing. Each step has ten
// lines; one is picked at random per step, once per start.
class StageQuips
{
public:
    enum class Step
    {
        Connecting, // opening audio and MIDI
        Unpacking,  // scanning plugins
        Setlist,    // loading the setlist
        WarmingUp,  // loading the setlist's sounds
        LineCheck,  // naming each plugin found
        SoundGuy,   // opening the window
        Ready,
        Count
    };

    // Every line for a step (for tests, and for anyone editing them).
    [[nodiscard]] static QStringList lines(Step step);

    // This start's line for `step`: random the first time, then the same.
    [[nodiscard]] QString line(Step step);

private:
    std::array<std::optional<QString>, static_cast<std::size_t>(Step::Count)> m_picked;
};

} // namespace gigchain::ui
