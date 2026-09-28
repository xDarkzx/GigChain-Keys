#include "MidiClockOut.h"

#include "EngineLog.h"
#include "MidiInput.h"

#include "gigchain/core/Branding.h"
#include "gigchain/engine/MidiSetup.h"

#include <rtmidi/RtMidi.h>

#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

MidiClockOut::MidiClockOut() = default;

MidiClockOut::~MidiClockOut()
{
    close();
}

QStringList MidiClockOut::listPorts()
{
    QStringList names;
    try {
        RtMidiOut out;
        const unsigned int count = out.getPortCount();
        for (unsigned int i = 0; i < count; ++i) names << QString::fromStdString(out.getPortName(i));
    } catch (const std::exception& e) {
        qCWarning(lcEngine).noquote() << "Listing MIDI outputs failed:" << QString::fromUtf8(e.what());
    }
    return portNames(names);
}

core::Result<void> MidiClockOut::open(const QString& port)
{
    close();
    if (port.isEmpty()) return {};
    const QStringList ports = listPorts();
    const qsizetype index = ports.indexOf(port);
    if (index < 0) {
        return core::fail(core::ErrorCode::DeviceUnavailable, u"No MIDI output named \"%1\" to send the clock to"_s.arg(port));
    }
    try {
        m_out = std::make_unique<RtMidiOut>();
        m_out->openPort(static_cast<unsigned int>(index), branding::name().toStdString());
        m_out->sendMessage(std::array<unsigned char, 1>{0xFA}.data(), 1); // Start
    } catch (const std::exception& e) {
        m_out.reset();
        const QString why = u"Could not send the MIDI clock to %1: %2"_s.arg(port, QString::fromUtf8(e.what()));
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::DeviceUnavailable, why);
    }
    m_port = port;
    m_ticks.store(0, std::memory_order_relaxed);
    m_stop.store(false, std::memory_order_relaxed);
    m_thread = std::thread([this] { run(); });
    qCInfo(lcEngine).noquote() << "MIDI clock out on" << port;
    return {};
}

void MidiClockOut::close()
{
    if (!m_thread.joinable()) return;
    m_stop.store(true, std::memory_order_relaxed);
    m_thread.join();
    try {
        m_out->sendMessage(std::array<unsigned char, 1>{0xFC}.data(), 1); // Stop
    } catch (const std::exception& e) {
        qCWarning(lcEngine).noquote() << "Sending MIDI Stop to" << m_port << "failed:" << QString::fromUtf8(e.what());
    }
    m_out.reset();
    qCInfo(lcEngine).noquote() << "MIDI clock out stopped on" << m_port << "after" << m_ticks.load() << "ticks";
    m_port.clear();
}

void MidiClockOut::run()
{
    // 1 ms scheduling for this thread's waits (Windows' default is 15.6 ms).
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    const std::array<unsigned char, 1> tick{0xF8};
    while (!m_stop.load(std::memory_order_relaxed)) {
        try {
            m_out->sendMessage(tick.data(), 1);
        } catch (const std::exception&) {
            // Reported by the engine's poll (takeFailed), once: a pulled
            // cable would otherwise flood the log with a failure per tick.
            m_failed.store(true, std::memory_order_relaxed);
            break;
        }
        m_ticks.fetch_add(1, std::memory_order_relaxed);
        const double bpm = std::clamp(m_tempo.load(std::memory_order_relaxed), 20.0, 400.0);
        next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(60.0 / (bpm * kClockTicksPerQuarter)));
        // Behind by more than a tick (the computer stalled): catch up from
        // now instead of sending a burst.
        if (Clock::now() > next + std::chrono::milliseconds(20)) next = Clock::now();
        std::this_thread::sleep_until(next);
    }
    timeEndPeriod(1);
}

} // namespace gigchain::engine
