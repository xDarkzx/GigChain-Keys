#pragma once

#include <QString>

#include <functional>

namespace gigchain::ui {

// Whether the system lets the app hear its audio inputs. The Mac asks the
// player once (the app's Info.plist says why); refused, its inputs give
// silence and no error at all, so the app checks and says so. Systems with
// nothing to ask (Windows, Linux) grant it. Through Qt's own microphone
// permission; the check and the question can be replaced (tests).
class InputPermission
{
public:
    enum class Answer
    {
        Granted,
        Denied,
        Undetermined, // not asked yet
    };
    using Check = std::function<Answer()>;
    using Ask = std::function<void(std::function<void(Answer)>)>;

    InputPermission(); // the system's, through Qt
    InputPermission(Check check, Ask ask);

    [[nodiscard]] Answer answer() const;

    // Before the inputs are heard: granted, nothing to do; not asked yet,
    // the player is asked, and `allowedNow` runs when they allow it (open the
    // input again: until then it was silent); refused, now or when asked,
    // `refused` gets what to say.
    void ensure(const std::function<void()>& allowedNow, const std::function<void(const QString&)>& refused) const;

    // What the player is told when refused: where to allow it.
    [[nodiscard]] static QString refusedMessage();

private:
    Check m_check;
    Ask m_ask;
};

} // namespace gigchain::ui
