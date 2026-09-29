#include "AudioApis.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine {

std::vector<AudioDriver> systemAudioDrivers()
{
    return {AudioDriver::System, AudioDriver::Asio};
}

RtAudio::Api toRtApi(AudioApi api)
{
    return api == AudioApi::Asio ? RtAudio::WINDOWS_ASIO : RtAudio::WINDOWS_WASAPI;
}

QString apiName(AudioApi api)
{
    switch (api) {
    case AudioApi::System: return u"WASAPI"_s;
    case AudioApi::Asio: return u"ASIO"_s;
    case AudioApi::Jack: return u"JACK"_s;
    case AudioApi::Alsa: return u"ALSA"_s;
    }
    return u"WASAPI"_s;
}

} // namespace gigchain::engine
