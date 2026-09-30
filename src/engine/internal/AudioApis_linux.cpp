#include "AudioApis.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine {

// As Reaper offers on Linux: PulseAudio (also on PipeWire desktops, and in
// WSLg) by default, JACK for pro audio, ALSA straight to the hardware.
std::vector<AudioDriver> systemAudioDrivers()
{
    return {AudioDriver::System, AudioDriver::Jack, AudioDriver::Alsa};
}

RtAudioStreamFlags latencyFlags(AudioApi /*api*/)
{
    return RTAUDIO_MINIMIZE_LATENCY; // ALSA: two periods; PulseAudio and JACK ignore it
}

RtAudio::Api toRtApi(AudioApi api)
{
    switch (api) {
    case AudioApi::Jack: return RtAudio::UNIX_JACK;
    case AudioApi::Alsa: return RtAudio::LINUX_ALSA;
    case AudioApi::System:
    case AudioApi::Asio: break; // (not on this system: never asked for)
    }
    return RtAudio::LINUX_PULSE;
}

QString apiName(AudioApi api)
{
    switch (api) {
    case AudioApi::System: return u"PulseAudio"_s;
    case AudioApi::Asio: return u"ASIO"_s;
    case AudioApi::Jack: return u"JACK"_s;
    case AudioApi::Alsa: return u"ALSA"_s;
    }
    return u"PulseAudio"_s;
}

} // namespace gigchain::engine
