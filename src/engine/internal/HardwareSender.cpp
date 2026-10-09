#include "HardwareSender.h"

#include "EngineLog.h"
#include "ExternalMidiOut.h"

#include <array>
#include <numeric>
#include <span>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

// How long an output that failed is left alone before it is tried again.
constexpr auto kRest = std::chrono::seconds(2);

// The bytes of one channel message (Program Change and channel pressure have one data byte).
std::size_t messageLength(uint8_t status)
{
    const uint8_t kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 2 : 3;
}

} // namespace

HardwareSender::HardwareSender(ExternalMidiOut& out) : m_out(out) {}

HardwareSender::~HardwareSender() = default; // m_thread stops and joins first (declared last)

void HardwareSender::setOuts(std::vector<std::shared_ptr<HardwareOut>> outs)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_outs = std::move(outs);
        if (m_outs.empty()) return; // a running thread idles (it is cheap); none starts
    }
    if (!m_thread.joinable()) {
        m_thread = std::jthread([this](const std::stop_token& stop) { sendUntilStopped(stop); });
        qCInfo(lcEngine) << "Hardware synths: sender thread started";
    }
}

void HardwareSender::allNotesOff()
{
    std::vector<std::shared_ptr<HardwareOut>> outs;
    {
        const std::scoped_lock lock(m_mutex);
        outs = m_outs;
    }
    for (const auto& out : outs) {
        const auto status = static_cast<unsigned char>(0xB0 | (out->midiChannel() - 1));
        // Sustain up, then All Notes Off (each synth's own panic).
        for (const std::array<unsigned char, 3>& message : {std::array<unsigned char, 3>{status, 64, 0}, std::array<unsigned char, 3>{status, 123, 0}}) {
            if (auto sent = m_out.send(out->port(), message); !sent) {
                const std::scoped_lock lock(m_mutex);
                m_problems.push_back(sent.error().message); // logged by send
                break;
            }
        }
    }
}

std::vector<QString> HardwareSender::takeProblems()
{
    const std::scoped_lock lock(m_mutex);
    return std::exchange(m_problems, {});
}

uint64_t HardwareSender::takeDropped()
{
    const std::scoped_lock lock(m_mutex);
    return std::accumulate(m_outs.begin(), m_outs.end(), m_dropped.exchange(0, std::memory_order_relaxed),
                           [](uint64_t sum, const auto& out) { return sum + out->takeDropped(); });
}

void HardwareSender::sendUntilStopped(const std::stop_token& stop)
{
    while (!stop.stop_requested()) {
        std::vector<std::shared_ptr<HardwareOut>> outs; // (a synth no channel plays any more is let go at the end)
        {
            const std::scoped_lock lock(m_mutex);
            outs = m_outs;
        }
        for (const auto& out : outs) sendFrom(*out);
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // (1 ms: the timer resolution is raised at start-up)
    }
}

void HardwareSender::sendFrom(HardwareOut& out)
{
    MidiEvent event;
    const auto now = std::chrono::steady_clock::now();
    auto resting = m_restingUntil.find(out.port());
    const bool rest = resting != m_restingUntil.end() && now < resting->second;
    while (out.pop(event)) {
        if (rest) {
            m_dropped.fetch_add(1, std::memory_order_relaxed); // its output is down: counted, said once
            continue;
        }
        const std::array<unsigned char, 3> bytes{event.status, event.data1, event.data2};
        if (auto sent = m_out.send(out.port(), std::span(bytes).first(messageLength(event.status))); !sent) {
            const bool saidAlready = resting != m_restingUntil.end();
            m_restingUntil[out.port()] = now + kRest;
            if (!saidAlready) {
                const std::scoped_lock lock(m_mutex);
                m_problems.push_back(u"The hardware synth on %1 cannot be played: %2"_s.arg(out.port(), sent.error().message));
            }
            return;
        }
        if (resting != m_restingUntil.end()) {
            m_restingUntil.erase(resting); // it works again
            resting = m_restingUntil.end();
            qCInfo(lcEngine).noquote() << "Hardware synth on" << out.port() << "reachable again";
        }
    }
}

} // namespace gigchain::engine
