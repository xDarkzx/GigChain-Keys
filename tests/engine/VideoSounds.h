#pragma once

// The feature videos' instrument sounds: one WAV per scene, played by the
// app's own instruments (Saw Stack Pro, Piano Pro) through their real
// engines, at a house tempo with the beat running (so the sidechain pumps on
// it), knobs moved while they play as the scene shows. Only made when asked
// (GIGCHAIN_VIDEO_SOUNDS=<folder>): see renderVideoSounds() in the tests.
#include "INode.h"

#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

namespace video {

inline constexpr double kTempo = 124.0;
inline constexpr double kRate = 48000.0;
inline constexpr int kBlock = 256;

struct Note
{
    double beat = 0.0;
    int key = 60;
    int velocity = 100;
    double length = 1.0; // beats
};
struct Move // a knob turned (or a type chosen) at a beat
{
    Move(double atBeat, uint32_t parameter, double to) : beat(atBeat), id(parameter), value(to) {}
    double beat = 0.0;
    uint32_t id = 0;
    double value = 0.0;
};
struct Sweep // a knob turned smoothly from one value to another
{
    Sweep(double fromBeat, double toBeat, uint32_t parameter, double first, double last)
        : from(fromBeat), to(toBeat), id(parameter), start(first), end(last)
    {
    }
    double from = 0.0, to = 4.0; // beats
    uint32_t id = 0;
    double start = 0.0, end = 1.0;
};
struct Pedal
{
    Pedal(double atBeat, bool isDown) : beat(atBeat), down(isDown) {}
    double beat = 0.0;
    bool down = true;
};
struct Take
{
    explicit Take(double length) : beats(length) {}
    void add(const Move& move) { moves.push_back(move); }
    void add(const Sweep& sweep) { sweeps.push_back(sweep); }
    void add(const Pedal& pedal) { pedals.push_back(pedal); }
    double beats = 8.0; // how long, tail included
    std::vector<Note> notes;
    std::vector<Move> moves;
    std::vector<Sweep> sweeps;
    std::vector<Pedal> pedals;
};

// The video's narration (its subs.json, from make_subs.py): when each word
// is said, so what changes in the sound changes on the word that names it,
// on the beat after it (feature video scenes start their picture, and so
// their sound, kLead before the voice).
class Cues
{
public:
    static constexpr double kLead = 0.35; // seconds of picture (and sound) before the voice
    static constexpr double kTail = 2.6;  // a scene goes on this long past its last word

    explicit Cues(const QString& folder)
    {
        QFile file(folder + QStringLiteral("/subs.json"));
        if (file.open(QIODevice::ReadOnly)) m_scenes = QJsonDocument::fromJson(file.readAll()).object();
    }
    [[nodiscard]] bool loaded() const { return !m_scenes.isEmpty(); }
    // The beat (of the scene's sound) on or after `word`'s `nth` saying;
    // `fallback` when the narration does not say it.
    [[nodiscard]] double beatOf(const QString& scene, const QString& wanted, double fallback, int nth = 1) const
    {
        int seen = 0;
        const QJsonArray words = m_scenes.value(scene).toObject().value(QStringLiteral("words")).toArray();
        for (const QJsonValue& v : words) {
            QString said = v.toObject().value(QStringLiteral("word")).toString().toLower();
            said.removeIf([](QChar c) { return !c.isLetterOrNumber() && c != u'\''; });
            if (said.startsWith(wanted.toLower()) && ++seen == nth) {
                return std::ceil((v.toObject().value(QStringLiteral("start")).toDouble() + kLead) / (60.0 / kTempo));
            }
        }
        return fallback;
    }
    // How many beats the scene's sound runs: past the scene's end.
    [[nodiscard]] double beatsOf(const QString& scene, double fallback) const
    {
        const double seconds = m_scenes.value(scene).toObject().value(QStringLiteral("duration")).toDouble(-1.0);
        return seconds < 0.0 ? fallback : std::ceil((seconds + kLead + kTail + 1.0) / (60.0 / kTempo));
    }

private:
    QJsonObject m_scenes;
};

// One chord: every key at `beat`, held `length` beats.
inline void chord(Take& take, double beat, std::initializer_list<int> keys, double length, int velocity = 100)
{
    for (const int key : keys) take.notes.push_back({.beat = beat, .key = key, .velocity = velocity, .length = length});
}

// Plays `take` on `synth` (any of the app's own: setParam, process) and
// returns it, left and right one after the other per frame.
template <typename Synth>
std::vector<float> render(Synth& synth, const Take& take)
{
    const double samplesPerBeat = kRate * 60.0 / kTempo;
    const auto total = static_cast<int64_t>(take.beats * samplesPerBeat);
    struct Event
    {
        int64_t at = 0;
        gigchain::engine::MidiEvent midi;
    };
    std::vector<Event> events;
    const auto when = [&](double beat) { return static_cast<int64_t>(beat * samplesPerBeat); };
    for (const Note& n : take.notes) {
        events.push_back({when(n.beat), {.status = 0x90, .data1 = static_cast<uint8_t>(n.key), .data2 = static_cast<uint8_t>(n.velocity)}});
        events.push_back({when(n.beat + n.length), {.status = 0x80, .data1 = static_cast<uint8_t>(n.key), .data2 = 0}});
    }
    std::ranges::transform(take.pedals, std::back_inserter(events), [&](const Pedal& p) {
        return Event{when(p.beat), {.status = 0xB0, .data1 = 64, .data2 = static_cast<uint8_t>(p.down ? 127 : 0)}};
    });
    std::ranges::stable_sort(events, {}, &Event::at);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total) * 2);
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    std::vector<gigchain::engine::MidiEvent> block;
    std::size_t next = 0;
    for (int64_t at = 0; at < total; at += kBlock) {
        const int frames = static_cast<int>(std::min<int64_t>(kBlock, total - at));
        const double beat = static_cast<double>(at) / samplesPerBeat;
        for (const Move& m : take.moves) {
            if (when(m.beat) >= at && when(m.beat) < at + frames) synth.setParam(m.id, m.value);
        }
        for (const Sweep& s : take.sweeps) {
            if (beat < s.from || beat > s.to) continue;
            const double t = (beat - s.from) / std::max(s.to - s.from, 1e-9);
            synth.setParam(s.id, s.start + ((s.end - s.start) * t));
        }
        block.clear();
        for (; next < events.size() && events.at(next).at < at + frames; ++next) {
            gigchain::engine::MidiEvent e = events.at(next).midi;
            e.sampleOffset = static_cast<int32_t>(events.at(next).at - at);
            block.push_back(e);
        }
        const gigchain::engine::TimeInfo time{.tempo = kTempo, .sampleRate = kRate, .samplePosition = at, .ppqPosition = beat};
        synth.process(block, gigchain::engine::AudioBlock{.left = left.data(), .right = right.data(), .frames = frames}, time);
        for (int i = 0; i < frames; ++i) {
            out.push_back(left.at(static_cast<std::size_t>(i)));
            out.push_back(right.at(static_cast<std::size_t>(i)));
        }
    }
    return out;
}

// Writes `frames` (left, right, ...) as a 16-bit stereo WAV at kRate.
inline bool writeWav(const QString& path, std::span<const float> frames)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    const auto dataBytes = static_cast<quint32>(frames.size() * 2);
    out.writeRawData("RIFF", 4);
    out << quint32{36 + dataBytes};
    out.writeRawData("WAVEfmt ", 8);
    out << quint32{16} << quint16{1} << quint16{2} << static_cast<quint32>(kRate) << static_cast<quint32>(kRate * 4) << quint16{4}
        << quint16{16};
    out.writeRawData("data", 4);
    out << dataBytes;
    for (const float s : frames) out << static_cast<qint16>(std::lround(std::clamp(s, -1.0F, 1.0F) * 32767.0F));
    return out.status() == QDataStream::Ok;
}

} // namespace video
