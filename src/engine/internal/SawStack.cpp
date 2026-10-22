#include "SawStack.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine::sawstack {

namespace {

constexpr std::array<const char*, kLayers> kLayerNames{"Sub", "Low Saw", "Main Saw", "Wide Saw",
                                                       "Octave Up", "Body", "Pluck", "Air"};
constexpr std::array<const char*, kLayers> kLayerKeys{"sub", "low", "main", "wide", "octave", "body", "pluck", "air"};
constexpr std::array<const char*, kLayerParams> kParamNames{
    "On",     "Level",  "Octave", "Voices",  "Detune", "Mix",   "Width",   "Wave",      "Cutoff",
    "Resonance", "Filter Envelope", "Filter Decay", "Attack", "Decay", "Sustain", "Release", "Low Cut", "Lowest Note Only"};
constexpr std::array<const char*, kLayerParams> kParamKeys{
    "on",     "level",  "octave", "voices",  "detune", "mix",   "width",   "wave",       "cutoff",
    "resonance", "filterEnv", "filterDecay", "attack", "decay", "sustain", "release", "lowCut", "lowestOnly"};
constexpr std::array<const char*, kGlobalParams> kGlobalNames{"Level", "Bend Range", "Velocity"};
constexpr std::array<const char*, kGlobalParams> kGlobalKeys{"level", "bendRange", "velocity"};
constexpr std::array<const char*, kMacros> kMacroNames{"Type",   "Cutoff", "Resonance", "Attack", "Release",        "Spread",
                                                       "Reverb", "Delay",  "OTT",       "Width",  "Sidechain",      "Sidechain Rate",
                                                       "Sidechain Slope", "Sidechain Curve", "Sidechain Trigger", "Sidechain On"};
constexpr std::array<const char*, kMacros> kMacroKeys{"type",   "cutoff", "resonance", "attack", "release",       "spread",
                                                      "reverb", "delay",  "ott",       "width",  "sidechain",     "sidechainRate",
                                                      "sidechainSlope", "sidechainCurve", "sidechainTrigger", "sidechainOn"};
constexpr auto kSidechainRates = static_cast<int>(sidechain::kRates.size());
constexpr auto kSidechainCurves = static_cast<int>(sidechain::kCurves.size());
constexpr auto kSidechainTriggers = static_cast<int>(sidechain::kTriggers.size());

constexpr float kSmoothing = 0.002F;       // per sample: about 10 ms at 48 kHz
constexpr std::size_t kSubLayer = 0;       // stays one clean sine whatever Spread says
constexpr std::size_t kPluckLayer = 6;     // keeps its own short shape whatever Attack and Release say
constexpr auto kAir = static_cast<std::size_t>(kNoiseLayer);
constexpr double kOutputDb = -14.0;        // every type's level before its own trim
constexpr double kButterworthQ = 0.7071;

int typeCount()
{
    return static_cast<int>(stackTypes().size());
}

constexpr std::size_t index(uint32_t id)
{
    return static_cast<std::size_t>(id);
}

} // namespace

double typeValue(int index)
{
    return Ranges::fromChoice(std::clamp(index, 0, typeCount() - 1), typeCount());
}

int typeIndex(double value)
{
    return Ranges::choice(value, typeCount());
}

std::array<float, kParamCount> typeParams(int which)
{
    const StackType& type = stackType(which);
    std::array<float, kParamCount> p{};
    for (int l = 0; l < kLayers; ++l) {
        const LayerDesign& d = type.layers.at(static_cast<std::size_t>(l));
        const auto set = [&](LayerParam param, double v) { p.at(index(paramId(l, param))) = static_cast<float>(v); };
        set(LayerParam::On, d.on ? 1.0 : 0.0);
        set(LayerParam::Level, Ranges::fromLevelDb(d.levelDb));
        set(LayerParam::Octave, Ranges::fromOctave(d.octave));
        set(LayerParam::Voices, Ranges::fromVoices(d.voices));
        set(LayerParam::Detune, d.detune);
        set(LayerParam::Mix, d.mix);
        set(LayerParam::Width, d.width);
        set(LayerParam::Wave, Ranges::fromChoice(d.wave, 3));
        set(LayerParam::Cutoff, Ranges::fromHz(d.cutoffHz));
        set(LayerParam::Resonance, d.resonance);
        set(LayerParam::FilterEnv, d.filterEnv);
        set(LayerParam::FilterDecay, Ranges::fromSeconds(d.filterDecay));
        set(LayerParam::Attack, Ranges::fromSeconds(d.attack));
        set(LayerParam::Decay, Ranges::fromSeconds(d.decay));
        set(LayerParam::Sustain, d.sustain);
        set(LayerParam::Release, Ranges::fromSeconds(d.release));
        set(LayerParam::Highpass, Ranges::fromHz(d.lowCutHz));
        set(LayerParam::LowestOnly, d.lowestOnly ? 1.0 : 0.0);
    }
    const auto set = [&](uint32_t id, double v) { p.at(index(id)) = static_cast<float>(v); };
    set(paramId(GlobalParam::Level), Ranges::fromLevelDb(kOutputDb));
    set(paramId(GlobalParam::BendRange), 2.0 / 12.0);
    set(paramId(GlobalParam::Velocity), 0.3);
    set(paramId(Macro::Type), typeValue(which));
    set(paramId(Macro::Cutoff), 1.0); // the main filter starts open
    set(paramId(Macro::Resonance), 0.0);
    set(paramId(Macro::Attack), Ranges::fromSeconds(type.attack));
    set(paramId(Macro::Release), Ranges::fromSeconds(type.release));
    set(paramId(Macro::Spread), 0.5);
    set(paramId(Macro::Reverb), type.fx.reverb);
    set(paramId(Macro::Delay), type.fx.delay);
    set(paramId(Macro::Ott), type.fx.ott);
    set(paramId(Macro::Width), type.fx.width);
    set(paramId(Macro::Sidechain), type.fx.sidechain);
    const auto rate = std::ranges::find(sidechain::kRates, type.fx.sidechainBeats, &sidechain::Rate::beats);
    set(paramId(Macro::SidechainRate),
        Ranges::fromChoice(rate != sidechain::kRates.end() ? static_cast<int>(std::distance(sidechain::kRates.begin(), rate))
                                                           : sidechain::kDefaultRate,
                           kSidechainRates));
    set(paramId(Macro::SidechainSlope), type.fx.sidechainSlope);
    set(paramId(Macro::SidechainCurve), Ranges::fromChoice(type.fx.sidechainCurve, kSidechainCurves));
    set(paramId(Macro::SidechainTrigger), 0.0); // the song's beat
    set(paramId(Macro::SidechainOn), 1.0);
    return p;
}

std::array<float, kParamCount> defaultParams()
{
    return typeParams(kDefaultType);
}

QString layerName(int layer)
{
    return layer >= 0 && layer < kLayers ? QString::fromLatin1(kLayerNames.at(static_cast<std::size_t>(layer))) : QString();
}

QString paramName(uint32_t id)
{
    constexpr auto layerParams = static_cast<uint32_t>(kLayers * kLayerParams);
    if (id < layerParams) {
        return u"%1 %2"_s.arg(layerName(static_cast<int>(id) / kLayerParams),
                              QString::fromLatin1(kParamNames.at(index(id % kLayerParams))));
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
                              QString::fromLatin1(kParamKeys.at(index(id % kLayerParams))));
    }
    const uint32_t g = id - layerParams;
    if (g < static_cast<uint32_t>(kGlobalParams)) return u"global.%1"_s.arg(QString::fromLatin1(kGlobalKeys.at(index(g))));
    const uint32_t m = g - kGlobalParams;
    return m < static_cast<uint32_t>(kMacros) ? QString::fromLatin1(kMacroKeys.at(index(m))) : QString();
}

bool Synth::Voice::active() const
{
    // (Held counts even when only the lowest-note layers play it.)
    return keyDown || sustained || std::ranges::any_of(layers, [](const LayerVoice& lv) { return lv.amp.active(); });
}

Synth::Synth()
{
    loadType(kDefaultType);
    m_seen.fill(-1.0F); // everything is worked out at the first block
}

void Synth::prepare(double sampleRate, int maxBlock)
{
    m_rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_layerL.assign(static_cast<std::size_t>(std::max(maxBlock, 1)), 0.0F);
    m_layerR.assign(m_layerL.size(), 0.0F);
    m_subL.assign(m_layerL.size(), 0.0F);
    m_subR.assign(m_layerL.size(), 0.0F);
    m_fx.prepare(m_rate);
    m_seen.fill(-1.0F);
    m_fxType = -1;
    applyParams();
    m_fx.snap();
    for (std::size_t l = 0; l < m_bus.size(); ++l) m_bus.at(l).gain = m_d.layers.at(l).on ? m_d.layers.at(l).gain : 0.0F;
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
        // The same one again changes nothing (the knobs stay where they are).
        const int which = typeIndex(std::clamp(value, 0.0, 1.0));
        if (which != m_type.load(std::memory_order_relaxed)) loadType(which);
        return;
    }
    m_params.at(index(id)).store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_relaxed);
}

double Synth::param(uint32_t id) const noexcept
{
    if (id == kSidechainPositionId) return m_sidechainPosition.load(std::memory_order_relaxed);
    return id < static_cast<uint32_t>(kParamCount) ? m_params.at(index(id)).load(std::memory_order_relaxed) : -1.0;
}

bool Synth::holdsNotes() const noexcept
{
    return std::ranges::any_of(m_voices, [](const Voice& v) { return v.keyDown || v.sustained; });
}

int Synth::soundingNotes() const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_voices, [](const Voice& v) { return v.active() && !v.stolen; }));
}

void Synth::applyParams() noexcept
{
    std::array<float, kParamCount> now{};
    std::ranges::transform(m_params, now.begin(), [](const std::atomic<float>& p) { return p.load(std::memory_order_relaxed); });
    if (now == m_seen) return;
    m_seen = now;
    const auto v = [&](int l, LayerParam p) { return static_cast<double>(now.at(index(paramId(l, p)))); };
    const auto knob = [&](Macro m) { return static_cast<double>(now.at(index(paramId(m)))); };
    // The knobs over the whole stack: the spread, the envelope (the filter,
    // on everything as a synth's main filter, is the effects').
    const double spread = 2.0 * knob(Macro::Spread);
    const double attack = Ranges::seconds(knob(Macro::Attack));
    const double release = Ranges::seconds(knob(Macro::Release));
    for (int l = 0; l < kLayers; ++l) {
        const auto at = static_cast<std::size_t>(l);
        LayerSetup& s = m_d.layers.at(at);
        s.on = v(l, LayerParam::On) >= 0.5;
        s.gain = static_cast<float>(Ranges::levelGain(v(l, LayerParam::Level)));
        s.octave = Ranges::octave(v(l, LayerParam::Octave));
        s.wave = static_cast<Wave>(Ranges::choice(v(l, LayerParam::Wave), 3));
        const bool spreads = at != kSubLayer;
        s.spread.set(Ranges::voices(v(l, LayerParam::Voices)),
                     spreads ? std::min(v(l, LayerParam::Detune) * spread, 1.0) : v(l, LayerParam::Detune), v(l, LayerParam::Mix),
                     spreads ? std::min(v(l, LayerParam::Width) * spread, 1.0) : v(l, LayerParam::Width));
        s.cutoff = Ranges::hz(v(l, LayerParam::Cutoff));
        s.q = Ranges::q(v(l, LayerParam::Resonance));
        s.filterOctaves = Ranges::filterOctaves(v(l, LayerParam::FilterEnv));
        s.highpass = Ranges::hz(v(l, LayerParam::Highpass));
        s.lowestOnly = at != kAir && v(l, LayerParam::LowestOnly) >= 0.5;
        // Attack and Release are the knobs' (the pluck keeps its own short shape).
        s.ownEnvelope = at == kPluckLayer;
        s.attack = s.ownEnvelope ? Ranges::seconds(v(l, LayerParam::Attack)) : attack;
        s.decay = Ranges::seconds(v(l, LayerParam::Decay));
        s.sustain = v(l, LayerParam::Sustain);
        s.release = s.ownEnvelope ? Ranges::seconds(v(l, LayerParam::Release)) : release;
        s.filterDecay = Ranges::seconds(v(l, LayerParam::FilterDecay));
        s.filterRelease = Ranges::seconds(v(l, LayerParam::Release));
        LayerBus& bus = m_bus.at(at);
        bus.lowCutL.set(s.highpass, kButterworthQ, m_rate);
        bus.lowCutR.set(s.highpass, kButterworthQ, m_rate);
    }
    const LayerSetup& air = m_d.layers.at(kAir);
    m_d.noise = static_cast<NoiseColour>(Ranges::choice(v(kNoiseLayer, LayerParam::Wave), 3));
    m_d.noiseWidth = v(kNoiseLayer, LayerParam::Width);
    m_noiseHpL.set(air.highpass, kButterworthQ, m_rate);
    m_noiseHpR.set(air.highpass, kButterworthQ, m_rate);
    m_noiseLpL.set(air.cutoff, kButterworthQ, m_rate);
    m_noiseLpR.set(air.cutoff, kButterworthQ, m_rate);
    m_d.masterGain = static_cast<float>(Ranges::levelGain(now.at(index(paramId(GlobalParam::Level)))));
    m_d.bendRange = Ranges::bendRange(now.at(index(paramId(GlobalParam::BendRange))));
    m_d.velocity = now.at(index(paramId(GlobalParam::Velocity)));
    for (Voice& voice : m_voices) {
        if (voice.stolen) continue; // keeps fading out fast
        auto setup = m_d.layers.begin();
        for (LayerVoice& lv : voice.layers) setEnvelopes(lv, *setup++);
    }
    auto setup = m_d.layers.begin();
    for (LayerVoice& mono : m_mono) setEnvelopes(mono, *setup++);
    // The air swells in with the Attack knob when that is slower than its own.
    m_noiseEnv.set(std::max(Ranges::seconds(v(kNoiseLayer, LayerParam::Attack)), attack), air.decay, air.sustain, release, m_rate);
    // The effects: what the type fixes, when it changes; what the knobs set, always.
    const int type = typeIndex(knob(Macro::Type));
    if (type != m_fxType) {
        m_fxType = type;
        m_fx.setDesign(stackType(type).fx);
    }
    m_fx.setKnobs({.masterGain = m_d.masterGain,
                   .cutoffHz = Ranges::hz(knob(Macro::Cutoff)),
                   .q = 0.7071 + 9.0 * knob(Macro::Resonance),
                   .reverb = knob(Macro::Reverb),
                   .delay = knob(Macro::Delay),
                   .ott = knob(Macro::Ott),
                   .width = knob(Macro::Width),
                   .tape = 0.0,
                   .sidechain = knob(Macro::SidechainOn) >= 0.5 ? knob(Macro::Sidechain) : 0.0,
                   .curve = Ranges::choice(knob(Macro::SidechainCurve), kSidechainCurves),
                   .beats = sidechain::kRates.at(static_cast<std::size_t>(Ranges::choice(knob(Macro::SidechainRate), kSidechainRates))).beats,
                   .slope = knob(Macro::SidechainSlope),
                   .fromNotes = Ranges::choice(knob(Macro::SidechainTrigger), kSidechainTriggers) == 1});
}

void Synth::setEnvelopes(LayerVoice& lv, const LayerSetup& setup) const noexcept
{
    lv.amp.set(setup.attack, setup.decay, setup.sustain, setup.release, m_rate);
    // The filter opens at the start of a note and closes over its decay.
    lv.filter.set(0.0, setup.filterDecay, 0.0, setup.filterRelease, m_rate);
}

double Synth::noteHz(int note, const LayerSetup& setup) const noexcept
{
    return 440.0 * std::pow(2.0, (note - 69 + 12 * setup.octave + m_bend) / 12.0);
}

void Synth::process(std::span<const MidiEvent> events, const AudioBlock& io, const TimeInfo& time) noexcept
{
    applyParams();
    m_tempo = time.tempo > 0.0 ? time.tempo : 120.0;
    m_ppqStep = m_tempo / 60.0 / m_rate;
    // Where the beat is (the host's when it has one; else counted here).
    if (time.samplePosition > 0 || time.ppqPosition > 0.0) m_ppq = time.ppqPosition;
    if (m_releaseAll.exchange(false, std::memory_order_acquire)) releaseAll(false);
    const auto total = static_cast<std::size_t>(std::max(io.frames, 0));
    const std::span<float> left(io.left, total);
    const std::span<float> right(io.right, total);
    std::ranges::fill(left, 0.0F);
    std::ranges::fill(right, 0.0F);
    const std::size_t frames = std::min(total, m_layerL.size());
    std::size_t done = 0;
    for (const MidiEvent& event : events) {
        if (event.controlsOnly) continue;
        const auto at = std::clamp(static_cast<std::size_t>(std::max(event.sampleOffset, 0)), done, frames);
        if (at > done) render(left.subspan(done, at - done), right.subspan(done, at - done));
        done = at;
        handle(event);
    }
    if (frames > done) render(left.subspan(done, frames - done), right.subspan(done, frames - done));
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
        case 121: // reset controllers
            m_bend = 0.0;
            setSustain(false);
            break;
        case 123: releaseAll(false); break; // all notes off
        default: break;
        }
    }
}

void Synth::noteOn(int note, int velocity) noexcept
{
    const float vel = 1.0F - (m_d.velocity * (1.0F - (static_cast<float>(velocity) / 127.0F)));
    // The same note again: the voice it already has plays it again, from
    // where it is (no click, no doubled note).
    auto same = std::ranges::find_if(m_voices, [&](const Voice& v) { return v.note == note && v.active() && !v.stolen; });
    Voice* target = same != m_voices.end() ? &*same : nullptr;
    if (target == nullptr) {
        // Too many notes: the oldest (one let go first) fades out fast.
        const auto sounding = [](const Voice& v) { return v.active() && !v.stolen; };
        if (std::ranges::count_if(m_voices, sounding) >= kMaxNotes) {
            Voice* oldest = nullptr;
            for (Voice& v : m_voices) {
                if (!sounding(v)) continue;
                const bool released = !v.keyDown && !v.sustained;
                const bool oldestReleased = oldest != nullptr && !oldest->keyDown && !oldest->sustained;
                if (oldest == nullptr || (released && !oldestReleased) || (released == oldestReleased && v.order < oldest->order)) {
                    oldest = &v;
                }
            }
            if (oldest != nullptr) {
                oldest->stolen = true;
                oldest->keyDown = oldest->sustained = false;
                for (LayerVoice& lv : oldest->layers) lv.amp.fastRelease(m_rate);
            }
        }
        auto idle = std::ranges::find_if(m_voices, [](const Voice& v) { return !v.active(); });
        if (idle == m_voices.end()) { // all fading: the quietest is cut
            idle = std::ranges::min_element(m_voices, {}, [](const Voice& v) {
                return std::ranges::max(v.layers, {}, [](const LayerVoice& lv) { return lv.amp.level; }).amp.level;
            });
        }
        target = &*idle;
        // A fresh start: each saw from its own random point (as the original
        // hardware's free-running saws), so no two notes sound quite alike.
        *target = Voice{};
        auto setup = m_d.layers.begin();
        for (LayerVoice& lv : target->layers) {
            std::ranges::generate(lv.phase, [this] { return m_random.unit(); });
            setEnvelopes(lv, *setup++);
        }
    }
    target->note = note;
    target->velocity = vel;
    target->keyDown = true;
    target->sustained = false;
    target->order = ++m_order;
    auto setup = m_d.layers.begin();
    for (LayerVoice& lv : target->layers) {
        const LayerSetup& s = *setup++;
        if (s.lowestOnly || &lv == &target->layers.at(kAir)) continue;
        lv.amp.gateOn();
        lv.filter.gateOn();
    }
    m_noiseEnv.gateOn(); // the air comes in again with each chord
    m_fx.noteAt(m_ppq);
    updateMono();
}

void Synth::noteOff(int note) noexcept
{
    for (Voice& v : m_voices) {
        if (v.note != note || !v.keyDown) continue;
        v.keyDown = false;
        if (m_sustain) {
            v.sustained = true;
            continue;
        }
        for (LayerVoice& lv : v.layers) {
            lv.amp.gateOff();
            lv.filter.gateOff();
        }
    }
    updateMono();
}

void Synth::setSustain(bool down) noexcept
{
    m_sustain = down;
    if (down) return;
    for (Voice& v : m_voices) {
        if (!v.sustained) continue;
        v.sustained = false;
        for (LayerVoice& lv : v.layers) {
            lv.amp.gateOff();
            lv.filter.gateOff();
        }
    }
    updateMono();
}

void Synth::releaseAll(bool fast) noexcept
{
    m_sustain = false;
    for (Voice& v : m_voices) {
        v.keyDown = v.sustained = false;
        for (LayerVoice& lv : v.layers) {
            if (fast) lv.amp.fastRelease(m_rate);
            else lv.amp.gateOff();
            lv.filter.gateOff();
        }
        if (fast) v.stolen = true;
    }
    updateMono();
    if (fast) {
        for (LayerVoice& mono : m_mono) mono.amp.fastRelease(m_rate);
        m_noiseEnv.fastRelease(m_rate);
    }
}

void Synth::updateMono() noexcept
{
    // The lowest note still held (or held by the pedal), and how hard it was played.
    int lowest = -1;
    float velocity = 1.0F;
    for (const Voice& v : m_voices) {
        if ((v.keyDown || v.sustained) && (lowest < 0 || v.note < lowest)) {
            lowest = v.note;
            velocity = v.velocity;
        }
    }
    for (LayerVoice& mono : m_mono) {
        if (lowest < 0) {
            mono.amp.gateOff();
            mono.filter.gateOff();
            continue;
        }
        if (!mono.amp.active()) std::ranges::generate(mono.phase, [this] { return m_random.unit(); });
        // A new lowest note while one plays: the pitch moves, the sound
        // goes on (no restart, no click).
        if (mono.note < 0 || mono.amp.stage == Envelope::Stage::Release || !mono.amp.active()) {
            mono.amp.gateOn();
            mono.filter.gateOn();
        }
        mono.note = lowest;
        mono.velocity = velocity;
    }
    if (lowest < 0) m_noiseEnv.gateOff();
}

void Synth::render(std::span<float> left, std::span<float> right) noexcept
{
    const std::size_t frames = left.size();
    const std::span<float> layerL(m_layerL.data(), frames);
    const std::span<float> layerR(m_layerR.data(), frames);
    std::ranges::fill(std::span<float>(m_subL.data(), frames), 0.0F);
    std::ranges::fill(std::span<float>(m_subR.data(), frames), 0.0F);
    for (std::size_t l = 0; l < m_bus.size(); ++l) {
        const LayerSetup& setup = m_d.layers.at(l);
        LayerBus& bus = m_bus.at(l);
        const float target = setup.on ? setup.gain : 0.0F;
        if (target == 0.0F && bus.gain < 1e-5F) { // switched off (and faded out): costs nothing
            bus.gain = 0.0F;
            continue;
        }
        if (l == kAir) {
            renderNoise(frames);
        } else {
            std::ranges::fill(layerL, 0.0F);
            std::ranges::fill(layerR, 0.0F);
            if (setup.lowestOnly) {
                LayerVoice& mono = m_mono.at(l);
                if (mono.amp.active()) renderLayer(mono, setup, mono.note, mono.velocity, frames);
            } else {
                for (Voice& v : m_voices) {
                    LayerVoice& lv = v.layers.at(l);
                    if (lv.amp.active()) renderLayer(lv, setup, v.note, v.velocity, frames);
                }
            }
            // The layer's own low cut: each layer keeps to its own range.
            if (setup.highpass > 21.0) {
                std::ranges::transform(layerL, layerL.begin(), [&bus](float s) { return bus.lowCutL.highPass(s); });
                std::ranges::transform(layerR, layerR.begin(), [&bus](float s) { return bus.lowCutR.highPass(s); });
            }
        }
        // The sub goes round the effects (as producers keep it): the OTT's
        // low band and the reverb would only make it boom.
        const bool sub = l == kSubLayer;
        auto fromL = layerL.begin();
        auto fromR = layerR.begin();
        auto toL = sub ? std::span<float>(m_subL.data(), frames).begin() : left.begin();
        auto toR = sub ? std::span<float>(m_subR.data(), frames).begin() : right.begin();
        for (std::size_t i = 0; i < frames; ++i) {
            bus.gain += (target - bus.gain) * kSmoothing;
            *toL++ += *fromL++ * bus.gain;
            *toR++ += *fromR++ * bus.gain;
        }
    }
    // The sub goes round the effects, added after them.
    m_fx.process(left, right, std::span<const float>(m_subL.data(), frames), std::span<const float>(m_subR.data(), frames), m_tempo, m_ppq,
                 m_ppqStep, m_random);
    m_sidechainPosition.store(static_cast<float>(m_fx.sidechainPosition()), std::memory_order_relaxed);
}

void Synth::renderLayer(LayerVoice& lv, const LayerSetup& setup, int note, float velocity, std::size_t frames) noexcept
{
    const UnisonSpread& spread = setup.spread;
    const auto voices = static_cast<std::size_t>(spread.voices);
    const double hz = noteHz(note, setup);
    std::array<float, kMaxUnison> step{};
    for (std::size_t i = 0; i < voices; ++i) {
        step.at(i) = static_cast<float>(std::min(hz * (1.0 + spread.offset.at(i)) / m_rate, 0.45));
    }
    // A gentle low cut at the note itself (as the original hardware): the
    // detuned saws' rumble under the note goes, the note stays.
    const bool cutRumble = setup.wave != Wave::Sine && voices > 1;
    const auto hpCoef = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::min(hz, m_rate * 0.45) / m_rate));
    const bool filtered = setup.cutoff < 19000.0 || setup.filterOctaves > 0.0;
    const std::span<float> layerL(m_layerL.data(), frames);
    auto toR = m_layerR.begin();
    for (float& outL : layerL) {
        float left = 0.0F;
        float right = 0.0F;
        for (std::size_t i = 0; i < voices; ++i) {
            float& phase = lv.phase.at(i);
            const float s = oscillator(setup.wave, phase, step.at(i));
            left += s * spread.left.at(i);
            right += s * spread.right.at(i);
            phase += step.at(i);
            if (phase >= 1.0F) phase -= 1.0F;
        }
        if (cutRumble) {
            lv.hpOutL = hpCoef * (lv.hpOutL + left - lv.hpInL);
            lv.hpInL = left;
            lv.hpOutR = hpCoef * (lv.hpOutR + right - lv.hpInR);
            lv.hpInR = right;
            left = lv.hpOutL;
            right = lv.hpOutR;
        }
        const float env = lv.filter.tick();
        if (filtered) {
            if (lv.filterCountdown-- <= 0) {
                lv.filterCountdown = kFilterUpdate;
                const double cutoff = setup.cutoff * std::exp2(setup.filterOctaves * env);
                lv.lpL.set(cutoff, setup.q, m_rate);
                lv.lpR.set(cutoff, setup.q, m_rate);
            }
            left = lv.lpL.lowPass(left);
            right = lv.lpR.lowPass(right);
        }
        const float amp = lv.amp.tick() * velocity;
        outL += left * amp;
        *toR++ += right * amp;
    }
}

void Synth::renderNoise(std::size_t frames) noexcept
{
    const std::span<float> layerL(m_layerL.data(), frames);
    const std::span<float> layerR(m_layerR.data(), frames);
    if (!m_noiseEnv.active()) {
        std::ranges::fill(layerL, 0.0F);
        std::ranges::fill(layerR, 0.0F);
        return;
    }
    const auto pink = [](std::array<float, 7>& b, float white) {
        // Paul Kellet's pink noise filter (public domain), loudness matched to white.
        auto& [b0, b1, b2, b3, b4, b5, b6] = b;
        b0 = (0.99886F * b0) + (white * 0.0555179F);
        b1 = (0.99332F * b1) + (white * 0.0750759F);
        b2 = (0.96900F * b2) + (white * 0.1538520F);
        b3 = (0.86650F * b3) + (white * 0.3104856F);
        b4 = (0.55000F * b4) + (white * 0.5329522F);
        b5 = (-0.7616F * b5) - (white * 0.0168980F);
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + (white * 0.5362F);
        b6 = white * 0.115926F;
        return out * 0.2F;
    };
    const auto width = static_cast<float>(m_d.noiseWidth);
    auto toR = layerR.begin();
    for (float& outL : layerL) {
        float a = 0.0F;
        float b = 0.0F;
        switch (m_d.noise) {
        case NoiseColour::White:
            a = m_random.bipolar();
            b = m_random.bipolar();
            break;
        case NoiseColour::Pink:
            a = pink(m_pinkL, m_random.bipolar());
            b = pink(m_pinkR, m_random.bipolar());
            break;
        case NoiseColour::Crackle: // sparse clicks, as dust on a record
            a = m_random.unit() < 0.0015F ? m_random.bipolar() * 4.0F : 0.0F;
            b = m_random.unit() < 0.0015F ? m_random.bipolar() * 4.0F : 0.0F;
            break;
        }
        // Width 0: the same noise in both ears; 1: different in each.
        const float right = a + ((b - a) * width);
        const float env = m_noiseEnv.tick();
        outL = m_noiseLpL.lowPass(m_noiseHpL.highPass(a)) * env;
        *toR++ = m_noiseLpR.lowPass(m_noiseHpR.highPass(right)) * env;
    }
}

} // namespace gigchain::engine::sawstack
