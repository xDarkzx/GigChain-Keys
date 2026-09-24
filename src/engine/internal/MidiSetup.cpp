#include "gigchain/engine/MidiSetup.h"

#include <algorithm>

namespace gigchain::engine {

std::vector<MidiPort> resolveMidiInputs(const QStringList& present, const MidiSetup& setup)
{
    std::vector<MidiPort> ports;
    ports.reserve(static_cast<std::size_t>(present.size()));
    for (const QString& name : present) {
        const auto channel = setup.channels.find(name);
        ports.push_back(MidiPort{name, setup.configured && setup.enabled.contains(name),
                                 channel != setup.channels.end() ? std::clamp(channel->second, 0, 16) : 0});
    }
    const bool anyOn = std::any_of(ports.begin(), ports.end(), [](const MidiPort& p) { return p.enabled; });
    if (!anyOn && !ports.empty()) ports.front().enabled = true;
    return ports;
}

} // namespace gigchain::engine
