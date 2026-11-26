#include "gigchain/platform/KeepAwake.h"

#include "PlatformLog.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QStringList>

#include <optional>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

namespace {

// A desktop service that does not answer is not waited for long: this runs
// on the app's main thread, as the sound starts.
constexpr int kAnswerMs = 2000;

const QString kSaverService = u"org.freedesktop.ScreenSaver"_s;
const QString kSaverPath = u"/org/freedesktop/ScreenSaver"_s;

} // namespace

struct KeepAwake::System
{
    std::optional<uint> saverCookie; // the screen saver's (blanking and lock)
    QDBusUnixFileDescriptor idleLock; // logind's: held while open (automatic suspend)
    bool held = false;

    // Lets go of both; what could not be let go, said (the lock is let go
    // anyway: closing it is what logind waits for). A screen saver that did
    // not answer keeps its cookie, so the next hold(false) (or the way out)
    // asks again: still held until then.
    QString letGo()
    {
        QString problem;
        if (saverCookie) {
            QDBusMessage call = QDBusMessage::createMethodCall(kSaverService, kSaverPath, kSaverService, u"UnInhibit"_s);
            call << *saverCookie;
            const QDBusMessage answer = QDBusConnection::sessionBus().call(call, QDBus::Block, kAnswerMs);
            if (answer.type() == QDBusMessage::ErrorMessage)
                problem = u"the screen saver (%1): %2"_s.arg(kSaverService, answer.errorMessage());
            else
                saverCookie.reset();
        }
        idleLock = QDBusUnixFileDescriptor();
        held = saverCookie.has_value();
        return problem;
    }
};

KeepAwake::KeepAwake() : m_system(std::make_unique<System>()) {}

KeepAwake::~KeepAwake()
{
    if (!m_system->held) return;
    if (const QString problem = m_system->letGo(); !problem.isEmpty())
        qCWarning(lcPlatform).noquote() << u"Could not let the screen sleep again: %1"_s.arg(problem);
}

core::Result<void> KeepAwake::hold(bool on, const QString& reason)
{
    if (on == m_system->held) return {};
    if (!on) {
        if (const QString problem = m_system->letGo(); !problem.isEmpty())
            return core::fail(core::ErrorCode::SystemRefused, u"Could not let the screen sleep again: %1"_s.arg(problem));
        return {};
    }

    const QString app = QCoreApplication::applicationName();
    QStringList problems;
    // The desktop's screen saver: no blanking, no lock.
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!session.isConnected()) {
        problems << u"the screen saver: no session bus (%1)"_s.arg(session.lastError().message());
    } else {
        QDBusMessage call = QDBusMessage::createMethodCall(kSaverService, kSaverPath, kSaverService, u"Inhibit"_s);
        call << app << reason;
        const QDBusReply<uint> cookie = session.call(call, QDBus::Block, kAnswerMs);
        if (cookie.isValid())
            m_system->saverCookie = cookie.value();
        else
            problems << u"the screen saver (%1): %2"_s.arg(kSaverService, cookie.error().message());
    }
    // logind: the session never counts as idle, so it is not suspended for it.
    QDBusConnection system = QDBusConnection::systemBus();
    if (!system.isConnected()) {
        problems << u"logind: no system bus (%1)"_s.arg(system.lastError().message());
    } else {
        QDBusMessage call = QDBusMessage::createMethodCall(u"org.freedesktop.login1"_s, u"/org/freedesktop/login1"_s,
                                                           u"org.freedesktop.login1.Manager"_s, u"Inhibit"_s);
        call << u"idle"_s << app << reason << u"block"_s;
        const QDBusReply<QDBusUnixFileDescriptor> lock = system.call(call, QDBus::Block, kAnswerMs);
        if (lock.isValid() && lock.value().isValid())
            m_system->idleLock = lock.value();
        else
            problems << u"logind (org.freedesktop.login1): %1"_s.arg(lock.error().message());
    }

    if (!m_system->saverCookie && !m_system->idleLock.isValid())
        return core::fail(core::ErrorCode::SystemRefused,
                          u"The desktop did not keep the screen on: %1"_s.arg(problems.join(u"; "_s)));
    if (!problems.isEmpty()) { // (one of the two is held: the other is said, not an error)
        qCInfo(lcPlatform).noquote() << (m_system->saverCookie
                                             ? u"The screen is kept on; automatic suspend is not held off: %1"_s
                                             : u"Only automatic suspend is held off; the screen may still go dark: %1"_s)
                                            .arg(problems.join(u"; "_s));
    }
    m_system->held = true;
    return {};
}

bool KeepAwake::held() const { return m_system->held; }

} // namespace gigchain::platform
