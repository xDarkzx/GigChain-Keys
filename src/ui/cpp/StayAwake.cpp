#include "StayAwake.h"

#include "gigchain/engine/IEngine.h"
#include "gigchain/platform/KeepAwake.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

StayAwake::StayAwake(engine::IEngine& engine, Ask ask, bool enabled)
    : m_engine(engine), m_ask(std::move(ask)), m_enabled(enabled)
{
    if (!m_ask) {
        m_system = std::make_unique<platform::KeepAwake>();
        m_ask = [system = m_system.get()](bool on) {
            // What the desktop shows for it (GNOME's and KDE's "apps keeping the screen on").
            return system->hold(on, QCoreApplication::translate("StayAwake", "Playing live"));
        };
    }
}

StayAwake::~StayAwake() = default; // (KeepAwake lets go as it goes)

void StayAwake::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;
    m_refused = false; // (switched again: worth asking again)
    follow();
}

void StayAwake::poll()
{
    follow();
}

void StayAwake::follow()
{
    const bool sounding = m_engine.beatClock().running;
    if (!sounding) m_refused = false; // the next start asks again
    const bool wanted = m_enabled && sounding;
    if (wanted == m_holding || (wanted && m_refused)) return;
    if (auto asked = m_ask(wanted); !asked) {
        if (wanted) {
            m_refused = true;
            qCWarning(lcUi).noquote() << u"Could not keep the screen on while the sound runs: %1"_s.arg(asked.error().message);
            return;
        }
        // Not let go as asked: it is not held by the app any more either way (and it ends with the app).
        qCWarning(lcUi).noquote() << asked.error().message;
    } else {
        qCInfo(lcUi).noquote() << (wanted ? u"The screen is kept on while the sound runs"_s : u"The screen may sleep again"_s);
    }
    m_holding = wanted;
}

} // namespace gigchain::ui
