#include "MidiInput.h"

#include "EngineLog.h"

#include <algorithm>

#include "gigchain/core/Branding.h"
#include "gigchain/engine/MidiSetup.h"

#include <rtmidi/RtMidi.h>

#include <chrono>
#include <exception>
#include <span>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

std::optional<MidiEvent> parseMidi(std::span<const unsigned char> bytes) noexcept
{
    if (bytes.empty()) return std::nullopt;
    const unsigned char status = bytes.front();
    if (status < 0x80 || status >= 0xF0) return std::nullopt; // data byte first, or a system message
    const int type = status & 0xF0;
    const std::size_t length = (type == 0xC0 || type == 0xD0) ? 2 : 3;
    if (bytes.size() < length) return std::nullopt;
    const std::span<const unsigned char> data = bytes.subspan(1, length - 1);
    if (std::ranges::any_of(data, [](unsigned char b) { return b > 0x7F; })) return std::nullopt;
    return MidiEvent{.status = status,
                     .data1 = data.front(),
                     .data2 = length == 3 ? data.back() : static_cast<uint8_t>(0),
                     .sampleOffset = 0};
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
    return portNames(ports);
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
        const QString& name = names.at(i);
        const auto wanted = std::ranges::find_if(ports, [&](const MidiPort& p) { return p.name == name; });
        if (wanted == ports.end() || !wanted->enabled) {
            qCInfo(lcEngine).noquote() << "MIDI input off:" << name;
            continue;
        }
        auto port = std::make_unique<Port>();
        port->owner = this;
        port->name = name;
        port->channel = wanted->channel;
        try {
            port->in = std::make_unique<RtMidiIn>();
            port->in->setErrorCallback(
                [](RtMidiError::Type, const std::string& text, void* user) {
                    qCWarning(lcEngine).noquote()
                        << "MIDI input" << static_cast<Port*>(user)->name << "reported:" << QString::fromStdString(text);
                },
                port.get());
            port->in->openPort(static_cast<unsigned int>(i), branding::name().toStdString());
            port->in->ignoreTypes(true, false, true); // sysex and active sensing; timing (MIDI clock) is read
            port->in->setCallback(&MidiInput::callback, port.get());
        } catch (const std::exception& e) {
            notices.push_back(u"Could not open MIDI input %1: %2"_s.arg(name, QString::fromUtf8(e.what())));
            qCWarning(lcEngine).noquote() << notices.back();
            continue;
        }
        qCInfo(lcEngine).noquote() << "MIDI input opened:" << name
                                   << (port->channel == 0 ? u"(all channels)"_s : u"(channel %1 only)"_s.arg(port->channel));
        m_ports.push_back(std::move(port));
    }
    return notices;
}

void MidiInput::close()
{
    for (const auto& port : m_ports) {
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
    auto next = out.begin();
    for (const auto& port : m_ports) {
        MidiEvent event;
        while (next != out.end() && port->queue.pop(event)) *next++ = event;
    }
    return static_cast<std::size_t>(next - out.begin());
}

// The signature is RtMidi's (RtMidiIn::RtMidiCallback): the message cannot be const.
// cppcheck-suppress constParameterCallback
void MidiInput::callback(double, std::vector<unsigned char>* message, void* user)
{
    auto* port = static_cast<Port*>(user);
    if (message == nullptr) return;
    if (!message->empty()) {
        switch (message->front()) {
        case 0xF8: port->owner->onClockTick(*port); return; // MIDI clock
        case 0xFA: port->owner->m_clockStart.store(true, std::memory_order_relaxed); return; // Start
        case 0xFB:                                                                          // Continue
        case 0xFC: return;                                                                  // Stop
        default: break;
        }
    }
    const auto event = parseMidi(*message);
    if (!event || !passesChannelFilter(event->status, port->channel)) return;
    if ((event->status & 0xF0) == 0x90 && event->data2 > 0) {
        port->owner->m_activity.store(true, std::memory_order_relaxed);
    }
    if (!port->queue.push(*event)) port->owner->m_dropped.fetch_add(1, std::memory_order_relaxed);
}

namespace {

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

void MidiInput::onClockTick(Port& port)
{
    const int64_t now = nowNs();
    const int64_t previous = std::exchange(port.lastTickNs, now);
    m_lastClockNs.store(now, std::memory_order_relaxed);
    if (previous == 0) return;
    const double seconds = static_cast<double>(now - previous) / 1e9;
    // Ticks further apart than 20 BPM are a restart, not a tempo.
    if (seconds <= 0.0 || seconds > 60.0 / (20.0 * kClockTicksPerQuarter)) {
        port.tickSeconds = 0.0;
        return;
    }
    // Smoothed over about a beat: single ticks jitter by a millisecond or so.
    port.tickSeconds = port.tickSeconds <= 0.0 ? seconds : port.tickSeconds + ((seconds - port.tickSeconds) / 24.0);
    m_clockTempo.store(60.0 / (port.tickSeconds * kClockTicksPerQuarter), std::memory_order_relaxed);
}

double MidiInput::clockTempo() const
{
    constexpr int64_t kStaleNs = 500'000'000; // half a second without a tick: the clock stopped
    if (nowNs() - m_lastClockNs.load(std::memory_order_relaxed) > kStaleNs) return 0.0;
    return m_clockTempo.load(std::memory_order_relaxed);
}

} // namespace gigchain::engine
