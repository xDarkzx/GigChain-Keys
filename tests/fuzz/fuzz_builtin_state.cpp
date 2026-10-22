// The app's own instruments (Saw Stack Pro, Piano Pro) save their settings
// inside setlist files: a damaged or hostile file must not crash the app
// while reading them. Whatever bytes: restoring answers settings or an error
// (never inflating a bomb), every setting stays 0-1, and settings it accepts
// save and load back the same.
#include "PianoProNode.h"
#include "SawStackNode.h"

#include <QByteArray>
#include <QtLogging>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace gigchain::engine;

namespace {

void quiet(QtMsgType, const QMessageLogContext&, const QString&) {}

template <typename Node>
void check(const QByteArray& bytes, int parameterCount)
{
    Node node; // (unprepared: nothing is loaded or played)
    if (!node.restoreEncodedState(bytes)) return;
    for (int id = 0; id < parameterCount; ++id) {
        const double v = node.parameterValue(static_cast<uint32_t>(id));
        if (!std::isfinite(v) || v < 0.0 || v > 1.0) std::abort();
    }
    const auto saved = node.saveEncodedState();
    if (!saved) std::abort();
    Node again;
    if (!again.restoreEncodedState(*saved)) std::abort();
    for (int id = 0; id < parameterCount; ++id) {
        if (again.parameterValue(static_cast<uint32_t>(id)) != node.parameterValue(static_cast<uint32_t>(id))) std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static const bool silenced = (qInstallMessageHandler(quiet), true);
    (void)silenced;
    const QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes
    // Either one's header in front: the fuzzer reaches both readers' insides.
    check<SawStackNode>(QByteArray("GCB1") + bytes, sawstack::kParamCount);
    check<PianoProNode>(QByteArray("GCP1") + bytes, piano::kParamCount);
    check<SawStackNode>(bytes, sawstack::kParamCount);
    check<PianoProNode>(bytes, piano::kParamCount);
    return 0;
}
