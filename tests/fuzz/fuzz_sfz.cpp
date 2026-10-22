// Piano Pro reads SFZ maps (the bundled pianos'; a damaged install could
// change them). Whatever text: reading answers regions or an error naming
// the line, never crashes or hangs, and every region it accepts names a sample.
#include "Sfz.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace gigchain::engine;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes
    const auto regions = sfz::parse(text);
    if (!regions) {
        if (regions.error().message.isEmpty()) std::abort();
        return 0;
    }
    for (const sfz::Region& r : *regions) {
        if (r.sample.isEmpty()) std::abort();
    }
    return 0;
}
