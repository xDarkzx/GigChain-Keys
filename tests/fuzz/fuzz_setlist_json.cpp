// Setlist files come from anywhere (shared by email, synced, hand-edited).
// Whatever bytes: the reader answers a setlist or an error, never crashes;
// and a setlist it accepts saves and reads back the same.
#include "gigchain/core/SetlistJson.h"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes
    const auto read = gigchain::core::fromJson(bytes);
    if (!read) return 0; // refused: fine, as long as it said so
    const auto again = gigchain::core::fromJson(gigchain::core::toJson(*read));
    if (!again || !(*again == *read)) std::abort(); // saved and read back differently
    return 0;
}
