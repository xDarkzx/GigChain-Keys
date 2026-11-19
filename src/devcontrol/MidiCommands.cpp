// The live test control's MIDI: notes, controllers, any message and timed
// sequences, entering where a keyboard's do (IEngine::injectMidi).
#include "DevControl.h"

#include "gigchain/engine/IEngine.h"

#include <QTimer>

#include <memory>

using namespace Qt::StringLiterals;

namespace gigchain::devcontrol {
namespace {

// Argument `at` as a whole number in [low, high], or nothing.
std::optional<int> wholeArg(const DevRequest& request, qsizetype at, int low, int high, int fallback)
{
    if (at >= request.args.size()) return fallback;
    const QJsonValue value = request.args.at(at);
    if (!value.isDouble() || value.toDouble() != static_cast<double>(value.toInt())) return std::nullopt;
    const int number = value.toInt();
    if (number < low || number > high) return std::nullopt;
    return number;
}

QByteArray message(int status, int data1, int data2)
{
    QByteArray bytes;
    for (const int byte : {status, data1, data2}) bytes.append(static_cast<char>(byte));
    return bytes;
}

// Plays `steps` from `next` on, a timer between waits; answers once the last is sent.
void playFrom(engine::IEngine& engine, std::shared_ptr<std::vector<PlayStep>> steps, std::size_t next, QString keyboard, Reply reply)
{
    while (next < steps->size()) {
        const PlayStep& step = steps->at(next++);
        if (step.kind == PlayStep::Wait) {
            QTimer::singleShot(step.ms, Qt::PreciseTimer, [&engine, steps, next, keyboard, reply] { playFrom(engine, steps, next, keyboard, reply); });
            return;
        }
        if (auto played = engine.injectMidi(keyboard, step.bytes); !played) return reply.fail(u"Step %1: %2"_s.arg(next).arg(played.error().message));
    }
    reply.ok({{u"steps"_s, static_cast<qint64>(steps->size())}});
}

} // namespace

void addMidiCommands(DevControl& control)
{
    const auto play = [&control](const QString& keyboard, const QByteArray& bytes, const Reply& reply) {
        engine::IEngine* engine = control.context().engine;
        if (engine == nullptr) return reply.fail(u"No engine"_s);
        if (auto played = engine->injectMidi(keyboard, bytes); !played) return reply.fail(played.error().message);
        reply.ok({{u"sent"_s, QString::fromLatin1(bytes.toHex(' '))}});
    };
    control.add(u"note"_s, u"note <key> <velocity> [channel 1-16] [keyboard]: a key down (velocity 0: up), as a keyboard sends it"_s,
                [play](const DevRequest& request, const Reply& reply) {
        const auto key = wholeArg(request, 0, 0, 127, -1);
        const auto velocity = wholeArg(request, 1, 0, 127, -1);
        const auto channel = wholeArg(request, 2, 1, 16, 1);
        if (!key || *key < 0 || !velocity || *velocity < 0 || !channel) return reply.fail(u"note <key 0-127> <velocity 0-127> [channel 1-16] [keyboard]"_s);
        play(argText(request, 3), message((*velocity > 0 ? 0x90 : 0x80) | (*channel - 1), *key, *velocity), reply);
    });
    control.add(u"cc"_s, u"cc <controller> <value> [channel 1-16] [keyboard]: a knob, fader or pedal (64: sustain)"_s,
                [play](const DevRequest& request, const Reply& reply) {
        const auto controller = wholeArg(request, 0, 0, 127, -1);
        const auto value = wholeArg(request, 1, 0, 127, -1);
        const auto channel = wholeArg(request, 2, 1, 16, 1);
        if (!controller || *controller < 0 || !value || *value < 0 || !channel) return reply.fail(u"cc <controller 0-127> <value 0-127> [channel 1-16] [keyboard]"_s);
        play(argText(request, 3), message(0xB0 | (*channel - 1), *controller, *value), reply);
    });
    control.add(u"midi"_s, u"midi <hex bytes> [keyboard]: any one message, \"E0 00 40\" (pitch bend), \"C0 05\", \"FA\" (Start)"_s,
                [play](const DevRequest& request, const Reply& reply) {
        const auto bytes = parseHexMidi(argText(request, 0));
        if (!bytes) return reply.fail(bytes.error().message);
        play(argText(request, 1), *bytes, reply);
    });
    control.add(u"play"_s, u"play <script> [channel 1-16] [keyboard]: \"on 60 100; wait 50; off 60\" (cc, bend, pc too); answers when done"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        engine::IEngine* engine = control.context().engine;
        if (engine == nullptr) return reply.fail(u"No engine"_s);
        const auto channel = wholeArg(request, 1, 1, 16, 1);
        if (!channel) return reply.fail(u"play <script> [channel 1-16] [keyboard]"_s);
        auto steps = parsePlayScript(argText(request, 0), *channel);
        if (!steps) return reply.fail(steps.error().message);
        playFrom(*engine, std::make_shared<std::vector<PlayStep>>(std::move(*steps)), 0, argText(request, 2), reply);
    });
}

} // namespace gigchain::devcontrol
