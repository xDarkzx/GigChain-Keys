#include "PianoPro.h"

#include "SawStack.h"

#include "gigchain/engine/SidechainCurves.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace gigchain::engine::piano {

namespace {

using sawstack::Envelope;
using sawstack::Ranges;

constexpr std::array<const char*, kLayers> kLayerNames{"Piano", "Octave", "Strings", "Sub"};
constexpr std::array<const char*, kLayers> kLayerKeys{"piano", "octave", "strings", "sub"};
constexpr std::array<const char*, kLayerParams> kLayerParamNames{"On", "Level"};
constexpr std::array<const char*, kLayerParams> kLayerParamKeys{"on", "level"};
constexpr std::array<const char*, kGlobalParams> kGlobalNames{"Level", "Bend Range"};
constexpr std::array<const char*, kGlobalParams> kGlobalKeys{"level", "bendRange"};
constexpr std::array<const char*, kMacros> kMacroNames{"Type",    "Tone",      "Resonance",       "Attack",          "Release",
                                                       "Width",   "Reverb",    "Delay",           "OTT",             "Tape",
                                                       "Sidechain", "Sidechain Rate", "Sidechain Slope", "Sidechain Curve",
                                                       "Sidechain Trigger", "Sidechain On"};
constexpr std::array<const char*, kMacros> kMacroKeys{"type",    "tone",      "resonance",     "attack",         "release",
                                                      "width",   "reverb",    "delay",         "ott",            "tape",
                                                      "sidechain", "sidechainRate", "sidechainSlope", "sidechainCurve",
                                                      "sidechainTrigger", "sidechainOn"};
constexpr auto kRates = static_cast<int>(sidechain::kRates.size());
constexpr auto kCurves = static_cast<int>(sidechain::kCurves.size());
constexpr auto kTriggers = static_cast<int>(sidechain::kTriggers.size());

constexpr double kOutputDb = -14.0;  // every type's level before its own trim
// Each piano brought to about the same level going into the effects
// (VCSL's recordings are quiet; measured with a chord at velocity 100).
constexpr std::array<float, kSources> kSourceGainDb{24.0F, 22.0F, 22.0F, 20.0F};

float sourceGain(Source s)
{
    return std::pow(10.0F, kSourceGainDb.at(static_cast<std::size_t>(s)) / 20.0F);
}
constexpr float kSmoothing = 0.002F; // per sample: about 10 ms at 48 kHz
constexpr int kStringVoices = 5;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

constexpr std::size_t index(uint32_t id)
{
    return static_cast<std::size_t>(id);
}

int typeCount()
{
    return static_cast<int>(pianoTypes().size());
}

std::size_t at(Layer layer)
{
    return static_cast<std::size_t>(layer);
}

// A 16-bit stereo recording read between its frames (4-point Hermite).
float hermite(std::span<const int16_t> frames, std::size_t channel, double position)
{
    const auto length = static_cast<std::ptrdiff_t>(frames.size() / 2);
    const auto whole = static_cast<std::ptrdiff_t>(position);
    const auto t = static_cast<float>(position - static_cast<double>(whole));
    const auto sample = [&](std::ptrdiff_t i) {
        const std::ptrdiff_t clamped = std::clamp<std::ptrdiff_t>(i, 0, length - 1);
        return static_cast<float>(*std::next(frames.begin(), (clamped * 2) + static_cast<std::ptrdiff_t>(channel))) * (1.0F / 32768.0F);
    };
    const float y0 = sample(whole - 1);
    const float y1 = sample(whole);
    const float y2 = sample(whole + 1);
    const float y3 = sample(whole + 2);
    const float c1 = 0.5F * (y2 - y0);
    const float c2 = y0 - (2.5F * y1) + (2.0F * y2) - (0.5F * y3);
    const float c3 = (0.5F * (y3 - y0)) + (1.5F * (y1 - y2));
    return (((((c3 * t) + c2) * t) + c1) * t) + y1;
}

float perSample(double seconds, double rate)
{
    return static_cast<float>(std::exp(-1.0 / (std::max(seconds, 1e-4) * rate)));
}

} // namespace

QString layerName(Layer layer)
{
    return QString::fromLatin1(kLayerNames.at(at(layer)));
}

QString paramName(uint32_t id)
{
    constexpr auto layerParams = static_cast<uint32_t>(kLayers * kLayerParams);
    if (id < layerParams) {
        return u"%1 %2"_s.arg(QString::fromLatin1(kLayerNames.at(index(id / kLayerParams))),
                              QString::fromLatin1(kLayerParamNames.at(index(id % kLayerParams))));
    }
    const uint32_t g = id - layerParams;
    if (g < static_cast<uint32_t>(kGlobalParams)) return QString::fromLatin1(kGlobalNames.at(index(g)));
    const uint32_t m = g - kGlobalParams;
    return m < static_cast<uint32_t>(kMacros) ? QString::fromLatin1(kMacroNames.at(index(m))) : QString();
}

QString paramKey(uint32_t id)
{
    constexpr auto layerParams = static_cast<uint32_t>(kLayers * kLayerParams);
    if (id < layerParams) {
        return u"%1.%2"_s.arg(QString::fromLatin1(kLayerKeys.at(index(id / kLayerParams))),
                              QString::fromLatin1(kLayerParamKeys.at(index(id % kLayerParams))));
    }
    const uint32_t g = id - layerParams;
    if (g < static_cast<uint32_t>(kGlobalParams)) return u"global.%1"_s.arg(QString::fromLatin1(kGlobalKeys.at(index(g))));
    const uint32_t m = g - kGlobalParams;
    return m < static_cast<uint32_t>(kMacros) ? QString::fromLatin1(kMacroKeys.at(index(m))) : QString();
}

double typeValue(int which)
{
    return Ranges::fromChoice(std::clamp(which, 0, typeCount() - 1), typeCount());
}

int typeIndex(double value)
{
    return Ranges::choice(value, typeCount());
}

std::array<float, kParamCount> typeParams(int which)
{
    const PianoType& type = pianoType(which);
    std::array<float, kParamCount> p{};
    const auto set = [&](uint32_t id, double v) { p.at(index(id)) = static_cast<float>(v); };
    for (int l = 0; l < kLayers; ++l) {
        const auto layer = static_cast<Layer>(l);
        const LayerDesign& d = type.layers.at(static_cast<std::size_t>(l));
        set(paramId(layer, LayerParam::On), d.on ? 1.0 : 0.0);
        set(paramId(layer, LayerParam::Level), Ranges::fromLevelDb(d.levelDb));
    }
    set(paramId(GlobalParam::Level), Ranges::fromLevelDb(kOutputDb));
    set(paramId(GlobalParam::BendRange), 2.0 / 12.0);
    set(paramId(Macro::Type), typeValue(which));
    set(paramId(Macro::Tone), Ranges::fromHz(type.toneHz));
    set(paramId(Macro::Resonance), 0.0);
    set(paramId(Macro::Attack), Ranges::fromSeconds(type.attack));
    set(paramId(Macro::Release), Ranges::fromSeconds(type.release));
    set(paramId(Macro::Width), type.fx.width);
    set(paramId(Macro::Reverb), type.fx.reverb);
    set(paramId(Macro::Delay), type.fx.delay);
    set(paramId(Macro::Ott), type.fx.ott);
    set(paramId(Macro::Tape), type.tape);
    set(paramId(Macro::Sidechain), type.fx.sidechain);
    const auto rate = std::ranges::find(sidechain::kRates, type.fx.sidechainBeats, &sidechain::Rate::beats);
    set(paramId(Macro::SidechainRate),
        Ranges::fromChoice(rate != sidechain::kRates.end() ? static_cast<int>(std::distance(sidechain::kRates.begin(), rate))
                                                           : sidechain::kDefaultRate,
                           kRates));
    set(paramId(Macro::SidechainSlope), type.fx.sidechainSlope);
    set(paramId(Macro::SidechainCurve), Ranges::fromChoice(type.fx.sidechainCurve, kCurves));
    set(paramId(Macro::SidechainTrigger), 0.0);
    set(paramId(Macro::SidechainOn), type.sidechainOn ? 1.0 : 0.0);
    return p;
}

Synth::Synth()
{
    loadType(kDefaultType);
    m_seen.fill(-1.0F);
}

void Synth::prepare(double sampleRate, int maxBlock)
{
    m_rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto frames = static_cast<std::size_t>(std::max(maxBlock, 1));
    for (std::vector<float>& b : m_layerL) b.assign(frames, 0.0F);
    for (std::vector<float>& b : m_layerR) b.assign(frames, 0.0F);
    m_subL.assign(frames, 0.0F);
    m_subR.assign(frames, 0.0F);
    m_fx.prepare(m_rate);
    m_seen.fill(-1.0F);
    m_fxType = -1;
    applyParams();
    m_fx.snap();
    for (std::size_t l = 0; l < m_busGain.size(); ++l) m_busGain.at(l) = m_d.on.at(l) ? m_d.gain.at(l) : 0.0F;
}

void Synth::useRecordings(Source which, const std::atomic<const SampleSet*>* slot) noexcept
{
    m_slots.at(static_cast<std::size_t>(which)).store(slot, std::memory_order_release);
}

void Synth::loadType(int which) noexcept
{
    const auto params = typeParams(which);
    m_type.store(typeIndex(params.at(index(paramId(Macro::Type)))), std::memory_order_relaxed);
    auto from = params.begin();
    for (std::atomic<float>& slot : m_params) slot.store(*from++, std::memory_order_relaxed);
}

void Synth::setParam(uint32_t id, double value) noexcept
{
    if (id >= static_cast<uint32_t>(kParamCount) || !std::isfinite(value)) return;
    if (id == paramId(Macro::Type)) {
        // Another type: all its layers and its knobs where it starts them.
        const int which = typeIndex(std::clamp(value, 0.0, 1.0));
        if (which != m_type.load(std::memory_order_relaxed)) loadType(which);
        return;
    }
    m_params.at(index(id)).store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_relaxed);
}

double Synth::param(uint32_t id) const noexcept
{
    if (id == sidechain::kPositionParameter) return m_sidechainPosition.load(std::memory_order_relaxed);
    return id < static_cast<uint32_t>(kParamCount) ? m_params.at(index(id)).load(std::memory_order_relaxed) : -1.0;
}

Source Synth::source() const noexcept
{
    return pianoType(m_type.load(std::memory_order_relaxed)).source;
}

bool Synth::holdsNotes() const noexcept
{
    return std::ranges::any_of(m_voices, [](const Voice& v) { return v.active() && (v.keyDown || v.sustained); });
}

int Synth::soundingNotes() const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_voices, [](const Voice& v) {
        return v.active() && !v.releaseSound && v.layer == static_cast<int>(Layer::Piano);
    }));
}

const SampleSet* Synth::recordings() const noexcept
{
    if (!recorded(m_source)) return nullptr;
    const auto* slot = m_slots.at(static_cast<std::size_t>(m_source)).load(std::memory_order_acquire);
    return slot != nullptr ? slot->load(std::memory_order_acquire) : nullptr;
}

void Synth::applyParams() noexcept
{
    std::array<float, kParamCount> now{};
    std::ranges::transform(m_params, now.begin(), [](const std::atomic<float>& p) { return p.load(std::memory_order_relaxed); });
    if (now == m_seen) return;
    m_seen = now;
    const auto knob = [&](Macro m) { return static_cast<double>(now.at(index(paramId(m)))); };
    for (int l = 0; l < kLayers; ++l) {
        const auto layer = static_cast<Layer>(l);
        const auto i = static_cast<std::size_t>(l);
        m_d.on.at(i) = now.at(index(paramId(layer, LayerParam::On))) >= 0.5F;
        m_d.gain.at(i) = static_cast<float>(Ranges::levelGain(now.at(index(paramId(layer, LayerParam::Level)))));
    }
    m_d.masterGain = static_cast<float>(Ranges::levelGain(now.at(index(paramId(GlobalParam::Level)))));
    m_d.bendRange = Ranges::bendRange(now.at(index(paramId(GlobalParam::BendRange))));
    m_d.attack = Ranges::seconds(knob(Macro::Attack));
    m_d.release = Ranges::seconds(knob(Macro::Release));
    const int type = typeIndex(knob(Macro::Type));
    if (type != m_fxType) {
        m_fxType = type;
        const PianoType& t = pianoType(type);
        m_fx.setDesign(t.fx);
        m_source = t.source;
        m_softness = t.softness;
        m_lowCutL.set(t.lowCutHz, 0.7071, m_rate);
        m_lowCutR.set(t.lowCutHz, 0.7071, m_rate);
    }
    m_fx.setKnobs({.masterGain = m_d.masterGain,
                   .cutoffHz = Ranges::hz(knob(Macro::Tone)),
                   .q = 0.7071 + 9.0 * knob(Macro::Resonance),
                   .reverb = knob(Macro::Reverb),
                   .delay = knob(Macro::Delay),
                   .ott = knob(Macro::Ott),
                   .width = knob(Macro::Width),
                   .tape = knob(Macro::Tape),
                   .sidechain = knob(Macro::SidechainOn) >= 0.5 ? knob(Macro::Sidechain) : 0.0,
                   .curve = Ranges::choice(knob(Macro::SidechainCurve), kCurves),
                   .beats = sidechain::kRates.at(static_cast<std::size_t>(Ranges::choice(knob(Macro::SidechainRate), kRates))).beats,
                   .slope = knob(Macro::SidechainSlope),
                   .fromNotes = Ranges::choice(knob(Macro::SidechainTrigger), kTriggers) == 1});
}

void Synth::process(std::span<const MidiEvent> events, const AudioBlock& io, const TimeInfo& time) noexcept
{
    applyParams();
    m_tempo = time.tempo > 0.0 ? time.tempo : 120.0;
    m_ppqStep = m_tempo / 60.0 / m_rate;
    if (time.samplePosition > 0 || time.ppqPosition > 0.0) m_ppq = time.ppqPosition;
    if (m_releaseAll.exchange(false, std::memory_order_acquire)) releaseAll(false);
    const auto total = static_cast<std::size_t>(std::max(io.frames, 0));
    const std::span<float> left(io.left, total);
    const std::span<float> right(io.right, total);
    std::ranges::fill(left, 0.0F);
    std::ranges::fill(right, 0.0F);
    const std::size_t frames = std::min(total, m_subL.size());
    std::size_t done = 0;
    for (const MidiEvent& event : events) {
        if (event.controlsOnly) continue;
        const auto when = std::clamp(static_cast<std::size_t>(std::max(event.sampleOffset, 0)), done, frames);
        if (when > done) render(left.subspan(done, when - done), right.subspan(done, when - done));
        done = when;
        handle(event);
    }
    if (frames > done) render(left.subspan(done, frames - done), right.subspan(done, frames - done));
    const double seconds = static_cast<double>(frames) / m_rate;
    for (Voice& v : m_voices) {
        if (v.active() && v.keyDown) v.heldSeconds += seconds;
    }
}

void Synth::handle(const MidiEvent& event) noexcept
{
    const int kind = event.status & 0xF0;
    if (kind == 0x90 && event.data2 > 0) {
        noteOn(event.data1 & 0x7F, event.data2);
    } else if (kind == 0x80 || kind == 0x90) {
        noteOff(event.data1 & 0x7F);
    } else if (kind == 0xE0) {
        const int value = (event.data2 << 7) | event.data1;
        m_bend = (value - 8192) / 8192.0 * m_d.bendRange;
    } else if (kind == 0xB0) {
        switch (event.data1) {
        case 64: setSustain(event.data2 >= 64); break;
        case 120: releaseAll(true); break; // all sound off
        case 121:
            m_bend = 0.0;
            setSustain(false);
            break;
        case 123: releaseAll(false); break; // all notes off
        default: break;
        }
    }
}

Synth::Voice* Synth::freeVoice(uint64_t keepFrom) noexcept
{
    auto idle = std::ranges::find_if(m_voices, [](const Voice& v) { return !v.active(); });
    if (idle != m_voices.end()) return &*idle;
    // All busy (rare: notes past kMaxNotes fade first): the quietest older
    // one goes (release sounds and let-go notes are quietest first).
    Voice* quietest = nullptr;
    float lowest = 0.0F;
    for (Voice& v : m_voices) {
        if (v.order >= keepFrom) continue;
        const float score = (v.keyDown || v.sustained ? 1.0F : 0.0F) + v.amp.level;
        if (quietest == nullptr || score < lowest) {
            quietest = &v;
            lowest = score;
        }
    }
    return quietest != nullptr ? quietest : &m_voices.front();
}

void Synth::makeRoomForANote() noexcept
{
    // The notes sounding (each by its oldest voice), not counting faded ones.
    const auto sounding = [](const Voice& v) {
        return v.active() && !v.stolen && !v.releaseSound && v.layer != static_cast<int>(Layer::Sub);
    };
    std::array<uint64_t, 128> oldest{};
    int notes = 0;
    for (const Voice& v : m_voices) {
        if (!sounding(v) || v.note < 0 || v.note > 127) continue;
        uint64_t& first = oldest.at(static_cast<std::size_t>(v.note));
        if (first == 0) ++notes;
        if (first == 0 || v.order < first) first = v.order;
    }
    if (notes < kMaxNotes) return;
    int victim = -1;
    uint64_t when = 0;
    for (int note = 0; note < 128; ++note) {
        const uint64_t first = oldest.at(static_cast<std::size_t>(note));
        if (first != 0 && (victim < 0 || first < when)) {
            victim = note;
            when = first;
        }
    }
    for (Voice& v : m_voices) {
        if (!sounding(v) || v.note != victim) continue;
        v.stolen = true;
        v.keyDown = v.sustained = false;
        v.amp.fastRelease(m_rate); // a few milliseconds: no click
    }
}

int Synth::layersPlaying(int note) const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_voices, [note](const Voice& v) {
        return v.active() && v.note == note && !v.stolen && !v.releaseSound && v.layer != static_cast<int>(Layer::Sub);
    }));
}

void Synth::startRecorded(Voice& v, const SampleSet& set, int note, int playedVelocity, int pitchNote) noexcept
{
    const int lookup = std::clamp(pitchNote, 0, 127);
    v.zone = set.find(lookup, playedVelocity, false, m_roundRobin.at(static_cast<std::size_t>(note))++);
    if (v.zone == nullptr) {
        startFm(v, pitchNote, static_cast<float>(playedVelocity) / 127.0F);
        return;
    }
    const float struck = static_cast<float>(playedVelocity) / 127.0F;
    const float vt = set.velocityTrack;
    v.gain = v.zone->gain * ((1.0F - vt) + (vt * struck * struck)) * sourceGain(m_source);
    v.position = 0.0;
    v.step = std::exp2((pitchNote - v.zone->keyCenter + v.zone->tuneCents / 100.0) / 12.0) * set.sampleRate / m_rate;
    // Undamped top strings ring on (the map gives them a long release).
    const double release = v.zone->release >= 2.0 ? v.zone->release : m_d.release;
    v.amp.set(std::max(v.zone->attack, m_d.attack), 0.0, 1.0, release, m_rate);
    v.panL = v.panR = 1.0F;
}

void Synth::startFm(Voice& v, int pitchNote, float velocity) noexcept
{
    const bool dx = m_source == Source::Dx;
    v.zone = nullptr;
    v.freq = 440.0 * std::exp2((pitchNote - 69) / 12.0);
    v.phase = v.modPhase = v.tinePhase = v.tineModPhase = 0.0;
    // The body: a sine bent by another at the same pitch (index under 1:
    // warm, never buzzy); harder notes bend it more (brighter).
    v.modIndex = static_cast<float>(dx ? 0.25 + (0.6 * velocity) : 0.3 + (0.6 * velocity));
    v.modDecay = perSample(dx ? 2.0 : 1.2, m_rate);
    // The tine (or the DX's glass): a sine bent by one 14 times as fast,
    // ringing out quickly; struck harder, much more of it.
    v.tineIndex = static_cast<float>(0.5 + (3.0 * velocity * velocity));
    v.tineDecay = perSample(dx ? 0.1 : 0.04, m_rate);
    v.tineLevel = static_cast<float>(dx ? 0.5 + (0.3 * velocity) : 0.3 + (0.4 * velocity));
    v.tineLevelDecay = perSample(dx ? 0.6 : 0.3, m_rate);
    // (Without recordings, a recorded type's FM piano at the electric's level.)
    v.gain = (0.35F + (0.65F * velocity)) * sourceGain(dx ? Source::Dx : Source::Electric);
    // Low notes ring longer.
    const double decay = (dx ? 3.5 : 3.0) * std::exp2(-(pitchNote - 60) / 24.0);
    v.amp.set(std::max(0.001, m_d.attack), decay, 0.0, m_d.release, m_rate);
    const double pan = std::clamp((pitchNote - 64) / 64.0, -1.0, 1.0) * 0.25;
    const double angle = (pan + 1.0) * std::numbers::pi / 4.0;
    v.panL = static_cast<float>(std::cos(angle) * std::numbers::sqrt2);
    v.panR = static_cast<float>(std::sin(angle) * std::numbers::sqrt2);
}

void Synth::noteOn(int note, int velocity) noexcept
{
    // Felt and lo-fi play the softer recordings (soft hammers).
    const int played = std::max(1, static_cast<int>(std::lround(velocity * (1.0 - (0.6 * m_softness)))));
    const float struck = static_cast<float>(played) / 127.0F;
    // The same key again: the note it already plays is damped quickly (no doubling, no click).
    for (Voice& v : m_voices) {
        if (v.active() && v.note == note && !v.releaseSound && v.layer != static_cast<int>(Layer::Sub)) {
            v.keyDown = v.sustained = false;
            v.amp.fastRelease(m_rate);
        }
    }
    makeRoomForANote();
    const SampleSet* set = recordings();
    const uint64_t thisNote = m_order + 1; // this note's layers start from here
    m_newestNote = thisNote;
    const auto start = [&](Layer layer, int pitchNote) {
        Voice& v = *freeVoice(thisNote);
        v = Voice{};
        v.note = note;
        v.layer = static_cast<int>(layer);
        v.keyDown = true;
        v.order = ++m_order;
        v.velocity = struck;
        if (layer == Layer::Strings) {
            v.freq = 440.0 * std::exp2((pitchNote - 69) / 12.0);
            std::ranges::generate(v.unisonPhase, [this] { return m_random.unit(); });
            v.lowL.set(2500.0, 0.7071, m_rate);
            v.lowR.set(2500.0, 0.7071, m_rate);
            v.gain = 0.5F + (0.5F * struck);
            v.amp.set(std::max(0.15, m_d.attack), 0.3, 0.8, std::max(0.6, m_d.release), m_rate);
        } else if (set != nullptr) {
            startRecorded(v, *set, note, played, pitchNote);
        } else {
            startFm(v, pitchNote, struck);
        }
        v.amp.gateOn();
    };
    if (m_d.on.at(at(Layer::Piano))) start(Layer::Piano, note);
    if (m_d.on.at(at(Layer::Octave))) start(Layer::Octave, note + 12);
    if (m_d.on.at(at(Layer::Strings))) start(Layer::Strings, note + 24);
    m_fx.noteAt(m_ppq);
    updateSub();
}

void Synth::letGo(Voice& v) noexcept
{
    v.amp.gateOff();
    // The piano's own release sound (dampers falling), quieter the longer the key was held.
    if (v.layer != static_cast<int>(Layer::Piano) || v.zone == nullptr) return;
    const SampleSet* set = recordings();
    if (set == nullptr) return;
    const SampleZone* zone = set->find(v.note, std::max(1, static_cast<int>(std::lround(v.velocity * 127.0F))), true,
                                       m_roundRobin.at(static_cast<std::size_t>(v.note)));
    if (zone == nullptr) return;
    const double fade = std::pow(10.0, -zone->rtDecayDb * v.heldSeconds / 20.0);
    const float vt = set->velocityTrack;
    const float gain = zone->gain * ((1.0F - vt) + (vt * v.velocity * v.velocity)) * static_cast<float>(fade) * sourceGain(m_source);
    const int note = v.note;
    Voice& r = *freeVoice(m_newestNote);
    r = Voice{};
    r.note = note;
    r.layer = static_cast<int>(Layer::Piano);
    r.releaseSound = true;
    r.order = ++m_order;
    r.zone = zone;
    r.gain = gain;
    r.step = std::exp2((note - zone->keyCenter + zone->tuneCents / 100.0) / 12.0) * set->sampleRate / m_rate;
    r.amp.set(zone->attack, 0.0, 1.0, zone->release, m_rate);
    r.amp.gateOn();
}

void Synth::noteOff(int note) noexcept
{
    for (Voice& v : m_voices) {
        if (v.note != note || !v.keyDown || v.releaseSound) continue;
        v.keyDown = false;
        if (m_sustain) {
            v.sustained = true;
            continue;
        }
        letGo(v);
    }
    updateSub();
}

void Synth::setSustain(bool down) noexcept
{
    m_sustain = down;
    if (down) return;
    for (Voice& v : m_voices) {
        if (!v.sustained) continue;
        v.sustained = false;
        letGo(v);
    }
    updateSub();
}

void Synth::releaseAll(bool fast) noexcept
{
    m_sustain = false;
    for (Voice& v : m_voices) {
        v.keyDown = v.sustained = false;
        if (fast) v.amp.fastRelease(m_rate);
        else v.amp.gateOff();
    }
    m_subNote = -1;
}

void Synth::updateSub() noexcept
{
    // The sub follows the lowest key still held (or held by the pedal).
    int lowest = -1;
    for (const Voice& v : m_voices) {
        if (v.active() && !v.releaseSound && v.layer != static_cast<int>(Layer::Sub) && (v.keyDown || v.sustained)
            && (lowest < 0 || v.note < lowest)) {
            lowest = v.note;
        }
    }
    if (lowest == m_subNote) return;
    for (Voice& v : m_voices) {
        if (v.active() && v.layer == static_cast<int>(Layer::Sub)) v.amp.gateOff();
    }
    m_subNote = lowest;
    if (lowest < 0 || !m_d.on.at(at(Layer::Sub))) return;
    Voice& s = *freeVoice(m_newestNote);
    s = Voice{};
    s.note = lowest;
    s.layer = static_cast<int>(Layer::Sub);
    s.order = ++m_order;
    s.freq = 440.0 * std::exp2((lowest - 12 - 69) / 12.0);
    s.gain = 1.0F;
    s.amp.set(0.005, 0.0, 1.0, 0.15, m_rate);
    s.amp.gateOn();
}

void Synth::renderVoice(Voice& v, std::size_t frames, double bend) noexcept
{
    const auto layer = static_cast<std::size_t>(v.layer);
    const std::span<float> outL(m_layerL.at(layer).data(), frames);
    const std::span<float> outR(m_layerR.at(layer).data(), frames);
    auto toR = outR.begin();
    if (v.zone != nullptr) {
        const std::span<const int16_t> recording(v.zone->frames);
        const auto end = static_cast<double>(v.zone->length() - 2);
        const double step = v.step * bend;
        for (float& l : outL) {
            float& r = *toR++;
            if (v.position >= end) { // the recording is over
                v.amp.stage = Envelope::Stage::Idle;
                v.amp.level = 0.0F;
                break;
            }
            const float env = v.amp.tick() * v.gain;
            l += hermite(recording, 0, v.position) * env;
            r += hermite(recording, 1, v.position) * env;
            v.position += step;
        }
        return;
    }
    const double inc = v.freq * bend / m_rate;
    if (v.layer == static_cast<int>(Layer::Strings)) {
        sawstack::UnisonSpread spread;
        spread.set(kStringVoices, 0.3, 0.6, 0.8);
        for (float& l : outL) {
            float& r = *toR++;
            float sl = 0.0F;
            float sr = 0.0F;
            for (std::size_t i = 0; i < static_cast<std::size_t>(kStringVoices); ++i) {
                const auto dt = static_cast<float>(inc * (1.0 + spread.offset.at(i)));
                float& phase = v.unisonPhase.at(i);
                const float s = sawstack::oscillator(sawstack::Wave::Saw, phase, dt);
                sl += s * spread.left.at(i);
                sr += s * spread.right.at(i);
                phase += dt;
                if (phase >= 1.0F) phase -= 1.0F;
            }
            const float env = v.amp.tick() * v.gain * 0.3F;
            l += v.lowL.lowPass(sl) * env;
            r += v.lowR.lowPass(sr) * env;
        }
        return;
    }
    if (v.layer == static_cast<int>(Layer::Sub)) {
        for (float& l : outL) {
            float& r = *toR++;
            const auto s = static_cast<float>(std::sin(kTwoPi * v.phase)) * v.amp.tick() * v.gain * 0.5F;
            l += s;
            r += s;
            v.phase += inc;
            v.phase -= std::floor(v.phase);
        }
        return;
    }
    // The FM piano.
    const bool dx = m_source == Source::Dx;
    const double detune = dx ? 1.0017 : 1.0; // the DX's two slightly apart carriers: its shimmer
    for (float& l : outL) {
        float& r = *toR++;
        const auto body = static_cast<float>(std::sin(kTwoPi * v.phase + v.modIndex * std::sin(kTwoPi * v.modPhase)));
        const auto tine = static_cast<float>(std::sin(kTwoPi * v.tinePhase + v.tineIndex * std::sin(kTwoPi * v.tineModPhase)));
        const float env = v.amp.tick() * v.gain;
        const float s = ((0.8F * body) + (v.tineLevel * tine)) * env * 0.5F;
        l += s * v.panL;
        r += s * v.panR;
        v.phase += inc;
        v.phase -= std::floor(v.phase);
        v.modPhase += inc;
        v.modPhase -= std::floor(v.modPhase);
        v.tinePhase += inc * detune;
        v.tinePhase -= std::floor(v.tinePhase);
        v.tineModPhase += inc * 14.0;
        v.tineModPhase -= std::floor(v.tineModPhase);
        v.modIndex *= v.modDecay;
        v.tineIndex *= v.tineDecay;
        v.tineLevel *= v.tineLevelDecay;
    }
    // Decayed away (the FM piano's level falls to nothing while held).
    if (v.amp.stage == Envelope::Stage::Sustain && v.amp.level <= 1e-4F) v.amp.stage = Envelope::Stage::Idle;
}

void Synth::render(std::span<float> left, std::span<float> right) noexcept
{
    const std::size_t frames = left.size();
    for (std::size_t l = 0; l < static_cast<std::size_t>(kLayers); ++l) {
        std::ranges::fill(std::span<float>(m_layerL.at(l).data(), frames), 0.0F);
        std::ranges::fill(std::span<float>(m_layerR.at(l).data(), frames), 0.0F);
    }
    const double bend = std::exp2(m_bend / 12.0);
    for (Voice& v : m_voices) {
        if (v.active()) renderVoice(v, frames, bend);
    }
    // The layers at their levels; the sub to its own bus, round the effects.
    const std::span<float> subL(m_subL.data(), frames);
    const std::span<float> subR(m_subR.data(), frames);
    std::ranges::fill(subL, 0.0F);
    std::ranges::fill(subR, 0.0F);
    for (std::size_t l = 0; l < static_cast<std::size_t>(kLayers); ++l) {
        const float target = m_d.on.at(l) ? m_d.gain.at(l) : 0.0F;
        float& gain = m_busGain.at(l);
        const bool sub = l == at(Layer::Sub);
        auto fromL = m_layerL.at(l).begin();
        auto fromR = m_layerR.at(l).begin();
        auto toL = sub ? subL.begin() : left.begin();
        auto toR = sub ? subR.begin() : right.begin();
        for (std::size_t i = 0; i < frames; ++i) {
            gain += (target - gain) * kSmoothing;
            *toL++ += *fromL++ * gain;
            *toR++ += *fromR++ * gain;
        }
    }
    // The type's low cut on the piano (prog house cuts its bass away).
    std::ranges::transform(left, left.begin(), [this](float s) { return m_lowCutL.highPass(s); });
    std::ranges::transform(right, right.begin(), [this](float s) { return m_lowCutR.highPass(s); });
    m_fx.process(left, right, subL, subR, m_tempo, m_ppq, m_ppqStep, m_random);
    m_sidechainPosition.store(static_cast<float>(m_fx.sidechainPosition()), std::memory_order_relaxed);
}

} // namespace gigchain::engine::piano
