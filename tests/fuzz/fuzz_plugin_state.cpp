// A plugin's saved settings travel inside setlist files: a damaged or
// hostile file must not crash the app while reading them. Whatever bytes:
// decode() answers settings or an error (never inflating a bomb); settings
// it accepts encode and decode back the same.
#include "Vst3Node.h"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using gigchain::engine::Vst3Node;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes
    const auto state = Vst3Node::State::decode(bytes);
    if (!state) return 0;
    const auto again = Vst3Node::State::decode(state->encode());
    if (!again || again->component != state->component || again->controller != state->controller) std::abort();
    return 0;
}
