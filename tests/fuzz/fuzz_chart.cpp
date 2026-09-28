// Charts are pasted from web pages and imported from files. Whatever text:
// every chart reader and tidier finishes without crashing, and a chart
// written out settles (writing out what was read back gives the same text),
// so editing and saving a chart never slowly changes it.
#include "gigchain/core/Chart.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace gigchain::core;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes

    const Chart chart = parseChordPro(text);
    (void)chartSections(chart);
    const QString once = toChordPro(chart);
    if (toChordPro(parseChordPro(once)) != once) std::abort(); // drifts on every save

    (void)isChordLine(text);
    (void)isSectionName(text);
    (void)tidyChordSheet(text);
    (void)chordSheetToChordPro(text);
    const ImportedSheet imported = importChordSheet(text);
    (void)chartSections(parseChordPro(imported.chart));
    return 0;
}
