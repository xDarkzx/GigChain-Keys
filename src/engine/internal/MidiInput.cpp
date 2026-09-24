#include "MidiInput.h"

#include "EngineLog.h"

#include <algorithm>

#include "gigchain/engine/MidiSetup.h"

#include <rtmidi/RtMidi.h>

#include <exception>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

std::optional<MidiEvent> parseMidi(std::span<const unsigned char> bytes) noexcept
{
    if (bytes.empty()) return std::nullopt;
    const unsigned char status = bytes[0];
    if (status < 0x80 || status >= 0xF0) return std::nullopt; // data byte first, or a system message
    const int type = status & 0xF0;
    const std::size_t length = (type == 0xC0 || type == 0xD0) ? 2 : 3;
    if (bytes.size() < length) return std::nullopt;
    for (std::size_t i = 1; i < length; ++i) {
        if (bytes[i] > 0x7F) return std::nullopt;
    }
    return MidiEvent{status, bytes[1], length == 3 ? bytes[2] : static_cast<uint8_t>(0), 0};
}

MidiInput::MidiInput() = default;

MidiInput::~MidiInput()
{
    close();
}

QStringList MidiInput::listPorts()
{
    QStringList ports;
    try {
        RtMidiIn probe;
        // Route RtMidi's own messages into our log instead of stderr.
        probe.setErrorCallback([](RtMidiError::Type type, const std::string& text, void*) {
            if (type == RtMidiError::WARNING || type == RtMidiError::DEBUG_WARNING) {
                qCInfo(lcEngine).noquote() << "MIDI:" << QString::fromStdString(text);
            } else {
                qCWarning(lcEngine).noquote() << "MIDI:" << QString::fromStdString(text);
            }
        });
        for (unsigned int i = 0; i < probe.getPortCount(); ++i) {
            ports << QString::fromStdString(probe.getPortName(i));
        }
    } catch (const std::exception& e) {
        qCWarning(lcEngine).noquote() << "Listing MIDI inputs failed:" << QString::fromUtf8(e.what());
    }
    return ports;
}

std::vector<QString> MidiInput::openAll(const std::vector<MidiPort>& ports)
{
    close();
    std::vector<QString> notices;
    const QStringList names = listPorts();
    if (names.isEmpty()) {
        qCInfo(lcEngine) << "No MIDI inputs found";
        return notices;
    }
    for (qsizetype i = 0; i < names.size(); ++i) {
        const auto wanted = std::find_if(ports.begin(), ports.end(), [&](const MidiPort& p) { return p.name == names[i]; });
        if (wanted == ports.end() || !wanted->enabled) {
            qCInfo(lcEngine).noquote() << "MIDI input off:" << names[i];
            continue;
        }
        auto port = std::make_unique<Port>();
        port->owner = this;
        port->name = names[i];
        port->channel = wanted->channel;
        try {
            port->in = std::make_unique<RtMidiIn>();
            port->in->setErrorCallback(
                [](RtMidiError::Type, const std::string& text, void* user) {
                    qCWarning(lcEngine).noquote()
                        << "MIDI input" << static_cast<Port*>(user)->name << "reported:" << QString::fromStdString(text);
                },
                port.get());
            port->in->openPort(static_cast<unsigned int>(i), "OpenStage");
            port->in->ignoreTypes(true, true, true); // sysex, timing, active sensing
            port->in->setCallback(&MidiInput::callback, port.get());
        } catch (const std::exception& e) {
            notices.push_back(u"Could not open MIDI input %1: %2"_s.arg(names[i], QString::fromUtf8(e.what())));
            qCWarning(lcEngine).noquote() << notices.back();
            continue;
        }
        qCInfo(lcEngine).noquote() << "MIDI input opened:" << names[i]
                                   << (port->channel == 0 ? u"(all channels)"_s : u"(channel %1 only)"_s.arg(port->channel));
        m_ports.push_back(std::move(port));
    }
    return notices;
}

void MidiInput::close()
{
    for (auto& port : m_ports) {
        try {
            port->in->cancelCallback();
            port->in->closePort();
        } catch (const std::exception& e) {
            qCWarning(lcEngine).noquote() << "Closing MIDI input" << port->name << "failed:" << QString::fromUtf8(e.what());
        }
    }
    m_ports.clear();
}

QStringList MidiInput::openPortNames() const
{
    QStringList names;
    for (const auto& port : m_ports) names << port->name;
    return names;
}

std::size_t MidiInput::drain(std::span<MidiEvent> out) noexcept
{
    std::size_t count = 0;
    for (const auto& port : m_ports) {
        MidiEvent event;
        while (count < out.size() && port->queue.pop(event)) out[count++] = event;
    }
    return count;
}

void MidiInput::callback(double, std::vector<unsigned char>* message, void* user)
{
    auto* port = static_cast<Port*>(user);
    if (message == nullptr) return;
    const auto event = parseMidi(*message);
    if (!event || !passesChannelFilter(event->status, port->channel)) return;
    if ((event->status & 0xF0) == 0x90 && event->data2 > 0) {
        port->owner->m_activity.store(true, std::memory_order_relaxed);
    }
    if (!port->queue.push(*event)) port->owner->m_dropped.fetch_add(1, std::memory_order_relaxed);
}

} // namespace gigchain::engine
