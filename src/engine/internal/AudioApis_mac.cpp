#include "AudioApis.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine {

// The Mac: Core Audio, low-latency already (Reaper offers only it).
std::vector<AudioDriver> systemAudioDrivers()
{
    return {AudioDriver::System};
}

RtAudio::Api toRtApi(AudioApi /*api*/)
{
    return RtAudio::MACOSX_CORE; // (the only driver here)
}

RtAudioStreamFlags latencyFlags(AudioApi /*api*/)
{
    // Not "minimise latency": Core Audio would take the device's smallest
    // buffer (15 frames) instead of the player's; the player's choice is
    // already the latency asked for.
    return 0;
}

QString apiName(AudioApi api)
{
    switch (api) {
    case AudioApi::System: return u"Core Audio"_s;
    case AudioApi::Asio: return u"ASIO"_s;
    case AudioApi::Jack: return u"JACK"_s;
    case AudioApi::Alsa: return u"ALSA"_s;
    }
    return u"Core Audio"_s;
}

} // namespace gigchain::engine
