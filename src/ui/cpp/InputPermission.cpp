#include "InputPermission.h"

#include "gigchain/core/Branding.h"

#include <QCoreApplication>
#include <QPermissions>

#include <utility>

namespace gigchain::ui {
namespace {

InputPermission::Answer answerOf(Qt::PermissionStatus status)
{
    switch (status) {
    case Qt::PermissionStatus::Granted: return InputPermission::Answer::Granted;
    case Qt::PermissionStatus::Denied: return InputPermission::Answer::Denied;
    case Qt::PermissionStatus::Undetermined: break;
    }
    return InputPermission::Answer::Undetermined;
}

} // namespace

InputPermission::InputPermission()
    : InputPermission(
          [] { return answerOf(QCoreApplication::instance()->checkPermission(QMicrophonePermission{})); },
          [](std::function<void(Answer)> reply) {
              QCoreApplication::instance()->requestPermission(
                  QMicrophonePermission{}, QCoreApplication::instance(),
                  [reply = std::move(reply)](const QPermission& permission) { reply(answerOf(permission.status())); });
          })
{
}

InputPermission::InputPermission(Check check, Ask ask) : m_check(std::move(check)), m_ask(std::move(ask)) {}

InputPermission::Answer InputPermission::answer() const
{
    return m_check();
}

void InputPermission::ensure(const std::function<void()>& allowedNow, const std::function<void(const QString&)>& refused) const
{
    switch (answer()) {
    case Answer::Granted: return;
    case Answer::Denied: refused(refusedMessage()); return;
    case Answer::Undetermined:
        m_ask([allowedNow, refused](Answer given) {
            if (given == Answer::Granted) allowedNow();
            else refused(refusedMessage());
        });
        return;
    }
}

QString InputPermission::refusedMessage()
{
    return QCoreApplication::translate(
               "Settings", "%1 may not hear the audio inputs: they stay silent. Allow it in the system's settings, "
                           "Privacy & Security → Microphone, then choose the input again.")
        .arg(branding::name());
}

} // namespace gigchain::ui
