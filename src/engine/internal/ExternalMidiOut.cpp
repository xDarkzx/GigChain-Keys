#include "ExternalMidiOut.h"

#include "EngineLog.h"
#include "MidiClockOut.h"

#include "gigchain/core/Branding.h"

#include <rtmidi/RtMidi.h>

#include <exception>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

ExternalMidiOut::ExternalMidiOut() = default;
ExternalMidiOut::~ExternalMidiOut() = default;

core::Result<void> ExternalMidiOut::send(const QString& port, std::span<const unsigned char> bytes)
{
    const std::scoped_lock lock(m_mutex);
    auto it = m_open.find(port);
    if (it == m_open.end()) {
        const QStringList ports = MidiClockOut::listPorts();
        const qsizetype index = ports.indexOf(port);
        if (index < 0) {
            const QString why = u"No MIDI output named \"%1\": is the synth plugged in and on?"_s.arg(port);
            qCWarning(lcEngine).noquote() << why;
            return core::fail(core::ErrorCode::DeviceUnavailable, why);
        }
        try {
            auto out = std::make_unique<RtMidiOut>();
            out->openPort(static_cast<unsigned int>(index), branding::name().toStdString());
            it = m_open.emplace(port, std::move(out)).first;
            qCInfo(lcEngine).noquote() << "MIDI output open for external gear:" << port;
        } catch (const std::exception& e) {
            const QString why = u"Could not open the MIDI output %1 (another program, or the MIDI clock, may be using it): %2"_s.arg(
                port, QString::fromUtf8(e.what()));
            qCWarning(lcEngine).noquote() << why;
            return core::fail(core::ErrorCode::DeviceUnavailable, why);
        }
    }
    try {
        it->second->sendMessage(bytes.data(), bytes.size());
    } catch (const std::exception& e) {
        m_open.erase(it); // unplugged: it opens again when it is back
        const QString why = u"Could not send to the MIDI output %1: %2"_s.arg(port, QString::fromUtf8(e.what()));
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::DeviceUnavailable, why);
    }
    return {};
}

} // namespace gigchain::engine
