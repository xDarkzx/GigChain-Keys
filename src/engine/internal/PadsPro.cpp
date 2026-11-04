#include "PadsPro.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace gigchain::engine::pads {

namespace {

// The eight pads (the research notes say what each is built from). Levels in
// dB, balanced by measurement so every pad is as loud as the others
// (tst_pads_pro: theTypesAreEquallyLoud).
constexpr std::array<PadType, 8> kTypes{{
    // A Juno's pad: a saw, a pulse whose width breathes, a square an octave
    // down, a closed filter and its dual chorus.
    {.key = "warm", .name = "Warm", .about = "A Juno's pad: saw, breathing pulse and sub, chorus",
     .sawDb = -8.0, .saws = 1, .pulseDb = -8.0, .pulseWidth = 0.5, .pwmDepth = 0.22, .pwmHz = 0.45, .subDb = -12.0,
     .cutoffHz = 900.0, .resonance = 0.8, .keyTrack = 0.3, .filterLfoHz = 0.12, .filterLfoOctaves = 0.15, .highpassHz = 110.0,
     .driftCents = 3.0, .attack = 1.2, .release = 4.5, .chorus = 0.55, .width = 0.3, .reverb = 0.38, .reverbSeconds = 6.0,
     .reverbSize = 0.7, .reverbDampHz = 3500.0, .levelDb = 0.1},
    // A Jupiter-8 / Prophet stack: detuned saws, the filter slowly breathing, wide.
    {.key = "lush", .name = "Lush", .about = "Jupiter-8 and Prophet: detuned saws, slowly breathing, wide",
     .sawDb = -4.0, .saws = 7, .sawCents = 18.0, .subDb = -16.0, .subSine = true,
     .cutoffHz = 1300.0, .resonance = 0.75, .keyTrack = 0.35, .filterLfoHz = 0.1, .filterLfoOctaves = 0.25, .highpassHz = 100.0,
     .driftCents = 3.0, .attack = 1.5, .release = 5.0, .chorus = 0.35, .width = 0.6, .reverb = 0.5, .reverbSeconds = 8.0,
     .reverbSize = 0.85, .reverbDampHz = 4500.0, .shimmer = 0.05, .levelDb = -1.0},
    // An OB-Xa with its soft 2-pole filter nearly closed: dark and round.
    {.key = "dark", .name = "Dark", .about = "OB-Xa, filter nearly closed: dark, round, a long tail",
     .sawDb = -4.0, .saws = 5, .sawCents = 10.0, .subDb = -10.0, .subSine = true,
     .cutoffHz = 520.0, .resonance = 0.9, .keyTrack = 0.25, .filterLfoHz = 0.08, .filterLfoOctaves = 0.1, .highpassHz = 90.0,
     .driftCents = 3.0, .attack = 1.8, .release = 6.0, .chorus = 0.3, .width = 0.35, .reverb = 0.45, .reverbSeconds = 7.0,
     .reverbSize = 0.8, .reverbDampHz = 1800.0, .levelDb = -0.8},
    // Breathy and open: saws with breath noise, the lows cleared away.
    {.key = "air", .name = "Air", .about = "Breathy and open: saws and breath, light shimmer",
     .sawDb = -6.0, .saws = 7, .sawCents = 14.0, .noiseDb = -20.0,
     .cutoffHz = 4000.0, .resonance = 0.7, .keyTrack = 0.2, .filterLfoHz = 0.07, .filterLfoOctaves = 0.2, .highpassHz = 250.0,
     .driftCents = 3.0, .attack = 2.5, .release = 6.0, .chorus = 0.3, .width = 0.7, .reverb = 0.55, .reverbSeconds = 9.0,
     .reverbSize = 0.9, .reverbDampHz = 6000.0, .shimmer = 0.15, .levelDb = 1.1},
    // The worship classic: Warm's oscillators through the octave-up shimmer.
    {.key = "shimmer", .name = "Shimmer", .about = "The worship classic: a warm pad climbing an octave in its reverb",
     .sawDb = -8.0, .saws = 3, .sawCents = 8.0, .pulseDb = -8.0, .pulseWidth = 0.5, .pwmDepth = 0.2, .pwmHz = 0.4,
     .subDb = -14.0, .subSine = true, .cutoffHz = 1100.0, .resonance = 0.75, .keyTrack = 0.3, .filterLfoHz = 0.1,
     .filterLfoOctaves = 0.15, .highpassHz = 120.0, .driftCents = 3.0, .attack = 2.5, .release = 7.0, .chorus = 0.4,
     .width = 0.55, .reverb = 0.55, .reverbSeconds = 9.0, .reverbSize = 0.9, .reverbDampHz = 5000.0, .shimmer = 0.55,
     .levelDb = 0.9},
    // A CS-80 choir: saws and a narrow pulse through an "aah" vowel, a gentle vibrato.
    {.key = "choir", .name = "Choir", .about = "CS-80 choir: an \"aah\" vowel, ensemble and a gentle vibrato",
     .sawDb = -6.0, .saws = 3, .sawCents = 8.0, .pulseDb = -10.0, .pulseWidth = 0.35, .pwmDepth = 0.1, .pwmHz = 0.3,
     .choir = true, .cutoffHz = 3000.0, .resonance = 0.7, .keyTrack = 0.2, .highpassHz = 150.0, .driftCents = 2.0,
     .vibratoHz = 5.0, .vibratoCents = 12.0, .attack = 1.8, .release = 5.0, .chorus = 0.5, .ensemble = 0.3, .width = 0.5,
     .reverb = 0.5, .reverbSeconds = 7.0, .reverbSize = 0.8, .reverbDampHz = 4000.0, .levelDb = -8.0},
    // A string machine (Solina, VP-330): saws and their octave, the ensemble's shimmer.
    {.key = "strings", .name = "Strings", .about = "A string machine: saws and their octave, the ensemble's swirl",
     .sawDb = -6.0, .saws = 5, .sawCents = 12.0, .octaveSawDb = -9.0, .cutoffHz = 3000.0, .resonance = 0.7,
     .keyTrack = 0.4, .highpassHz = 300.0, .driftCents = 2.0, .attack = 0.8, .release = 3.5, .chorus = 0.2,
     .ensemble = 0.8, .width = 0.5, .reverb = 0.45, .reverbSeconds = 5.0, .reverbSize = 0.7, .reverbDampHz = 4500.0,
     .levelDb = 2.3},
    // A hymn organ held as a pad: drawbar sines and a slow rotary swirl.
    {.key = "organ", .name = "Organ", .about = "A hymn organ held as a pad: drawbars and a slow swirl",
     .drawbarsDb = {-9.0, -4.0, -8.0, -14.0}, .cutoffHz = 5000.0, .resonance = 0.7, .keyTrack = 0.0, .highpassHz = 80.0,
     .driftCents = 1.0, .vibratoHz = 6.5, .vibratoCents = 6.0, .attack = 1.5, .release = 4.0, .chorus = 0.6, .width = 0.5,
     .reverb = 0.4, .reverbSeconds = 6.0, .reverbSize = 0.75, .reverbDampHz = 3500.0, .levelDb = -5.0},
}};

constexpr std::array<double, kDrawbars> kDrawbarRatios{0.5, 1.0, 2.0, 3.0};
// The "aah" of a voice: its three strongest resonances (Hz), how sharp, how loud.
constexpr std::array<double, 3> kVowelHz{800.0, 1150.0, 2900.0};
constexpr std::array<double, 3> kVowelQ{5.0, 6.0, 8.0};
constexpr std::array<float, 3> kVowelGain{1.0F, 0.5F, 0.25F};
constexpr int kFilterUpdate = 16; // samples between filter cutoff updates

float dbGain(double db)
{
    return db <= -60.0 ? 0.0F : static_cast<float>(std::pow(10.0, db / 20.0));
}
float centsRatio(double cents)
{
    return static_cast<float>(std::exp2(cents / 1200.0));
}

} // namespace

std::span<const PadType> padTypes()
{
    return kTypes;
}
const PadType& padType(int index)
{
    return kTypes.at(static_cast<std::size_t>(std::clamp(index, 0, padTypeCount() - 1)));
}
int padTypeCount()
{
    return static_cast<int>(kTypes.size());
}
double typeValue(int index)
{
    return Ranges::fromChoice(index, padTypeCount());
}
int typeIndex(double value)
{
    return Ranges::choice(value, padTypeCount());
}

QString paramName(uint32_t id)
{
    switch (static_cast<Param>(id)) {
    case Param::Type: return u"Type"_s;
    case Param::Tone: return u"Tone"_s;
    case Param::Shimmer: return u"Shimmer"_s;
    case Param::Motion: return u"Motion"_s;
    case Param::Attack: return u"Attack"_s;
    case Param::Release: return u"Release"_s;
    case Param::Width: return u"Width"_s;
    case Param::Reverb: return u"Reverb"_s;
    case Param::Level: return u"Level"_s;
    case Param::Velocity: return u"Velocity"_s;
    case Param::Count: break;
    }
    return u"Parameter %1"_s.arg(id);
}

QString paramKey(uint32_t id)
{
    return paramName(id).toLower();
}

std::array<float, kParamCount> typeParams(int index)
{
    const PadType& t = padType(index);
    std::array<float, kParamCount> p{};
    p.at(static_cast<std::size_t>(Param::Type)) = static_cast<float>(typeValue(index));
    p.at(static_cast<std::size_t>(Param::Tone)) = 0.5F;
    p.at(static_cast<std::size_t>(Param::Shimmer)) = static_cast<float>(t.shimmer);
    p.at(static_cast<std::size_t>(Param::Motion)) = 0.5F;
    p.at(static_cast<std::size_t>(Param::Attack)) = static_cast<float>(Ranges::fromSeconds(t.attack));
    p.at(static_cast<std::size_t>(Param::Release)) = static_cast<float>(Ranges::fromSeconds(t.release));
    p.at(static_cast<std::size_t>(Param::Width)) = static_cast<float>(t.width);
    p.at(static_cast<std::size_t>(Param::Reverb)) = static_cast<float>(t.reverb);
    p.at(static_cast<std::size_t>(Param::Level)) = static_cast<float>(Ranges::fromLevelDb(0.0));
    p.at(static_cast<std::size_t>(Param::Velocity)) = 0.0F;
    return p;
}

std::array<float, kParamCount> defaultParams()
{
    return typeParams(0);
}

Synth::Synth()
{
    const auto start = defaultParams();
    for (std::size_t i = 0; i < m_params.size(); ++i) m_params.at(i).store(start.at(i), std::memory_order_relaxed);
    m_seen.fill(-1.0F);
}

void Synth::prepare(double sampleRate, int /*maxBlock*/)
{
    m_rate = sampleRate;
    m_chorus.prepare(sampleRate);
    m_width.prepare(sampleRate);
    m_reverb.prepare(sampleRate);
    m_ensembleL.allocate(static_cast<std::size_t>(0.02 * sampleRate) + 8);
    m_ensembleR.allocate(static_cast<std::size_t>(0.02 * sampleRate) + 8);
    m_seen.fill(-1.0F); // everything worked out again for this rate
    m_type = -1;
    applyParams();
}

void Synth::setParam(uint32_t id, double value) noexcept
{
    if (id >= static_cast<uint32_t>(kParamCount) || !std::isfinite(value)) return;
    const auto v = static_cast<float>(std::clamp(value, 0.0, 1.0));
    if (id == paramId(Param::Type)) {
        // A new pad brings its own knobs (as a preset does); Level and Velocity are the player's.
        const auto start = typeParams(typeIndex(v));
        for (const Param p : {Param::Tone, Param::Shimmer, Param::Motion, Param::Attack, Param::Release, Param::Width, Param::Reverb}) {
            m_params.at(static_cast<std::size_t>(p)).store(start.at(static_cast<std::size_t>(p)), std::memory_order_relaxed);
        }
    }
    m_params.at(id).store(v, std::memory_order_relaxed);
}

double Synth::param(uint32_t id) const noexcept
{
    return id < static_cast<uint32_t>(kParamCount) ? m_params.at(id).load(std::memory_order_relaxed) : -1.0;
}

void Synth::applyParams() noexcept
{
    bool changed = false;
    std::array<float, kParamCount> now{};
    for (std::size_t i = 0; i < now.size(); ++i) {
        now.at(i) = m_params.at(i).load(std::memory_order_relaxed);
        changed = changed || now.at(i) != m_seen.at(i);
    }
    if (!changed) return;
    m_seen = now;
    const auto at = [&now](Param p) { return static_cast<double>(now.at(static_cast<std::size_t>(p))); };
    const int index = typeIndex(at(Param::Type));
    const PadType& t = padType(index);
    m_type = index;
    m_d.type = &t;
    m_d.sawGain = dbGain(t.sawDb);
    m_d.octaveGain = dbGain(t.octaveSawDb);
    m_d.pulseGain = dbGain(t.pulseDb);
    m_d.subGain = dbGain(t.subDb);
    m_d.noiseGain = dbGain(t.noiseDb);
    for (std::size_t b = 0; b < kDrawbars; ++b) m_d.drawbarGain.at(b) = dbGain(t.drawbarsDb.at(b));
    // The unison: evenly across the spread, alternating ears, as loud with one saw as with seven.
    const int saws = std::clamp(t.saws, 1, kSaws);
    const double width = std::clamp(at(Param::Width), 0.0, 1.0);
    const float norm = 1.0F / std::sqrt(static_cast<float>(saws));
    for (int i = 0; i < kSaws; ++i) {
        const double p = saws == 1 ? 0.0 : -1.0 + 2.0 * i / (saws - 1);
        m_d.sawOffset.at(static_cast<std::size_t>(i)) = static_cast<float>(p * t.sawCents * 0.5);
        const double pan = width * std::abs(p) * ((i % 2) != 0 ? 1.0 : -1.0);
        const double angle = (pan + 1.0) * std::numbers::pi / 4.0;
        const bool used = i < saws;
        m_d.sawLeft.at(static_cast<std::size_t>(i)) = used ? norm * static_cast<float>(std::cos(angle) * std::numbers::sqrt2) : 0.0F;
        m_d.sawRight.at(static_cast<std::size_t>(i)) = used ? norm * static_cast<float>(std::sin(angle) * std::numbers::sqrt2) : 0.0F;
    }
    m_d.cutoffHz = t.cutoffHz * std::exp2((at(Param::Tone) - 0.5) * 4.0); // ±2 octaves
    m_d.motion = at(Param::Motion) * 2.0;
    m_d.attack = Ranges::seconds(at(Param::Attack));
    m_d.release = Ranges::seconds(at(Param::Release));
    m_d.velocity = static_cast<float>(at(Param::Velocity));
    m_d.masterGain = static_cast<float>(std::pow(10.0, (Ranges::levelDb(at(Param::Level)) + t.levelDb) / 20.0));
    for (Voice& v : m_voices) {
        if (v.active() && !v.stolen) v.amp.set(m_d.attack, 0.2, 1.0, m_d.release, m_rate);
    }

    m_lowCutL.set(t.highpassHz, 0.7071, m_rate);
    m_lowCutR.set(t.highpassHz, 0.7071, m_rate);
    m_chorus.setMix(t.chorus);
    m_ensembleMix = static_cast<float>(t.ensemble);
    m_width.setAmount(width);
    sawstack::Reverb::Settings verb;
    verb.decaySeconds = t.reverbSeconds;
    verb.size = t.reverbSize;
    verb.dampHz = t.reverbDampHz;
    verb.highCutHz = 9000.0;
    verb.lowCutHz = 200.0;
    verb.chorus = 0.3;
    m_reverb.set(verb, at(Param::Reverb), at(Param::Shimmer));
}

void Synth::process(std::span<const MidiEvent> events, const AudioBlock& io) noexcept
{
    applyParams();
    if (m_releaseAll.exchange(false, std::memory_order_acquire)) releaseAll(false);
    const auto total = static_cast<std::size_t>(std::max(io.frames, 0));
    const std::span<float> left(io.left, total);
    const std::span<float> right(io.right, total);
    std::ranges::fill(left, 0.0F);
    std::ranges::fill(right, 0.0F);
    std::size_t done = 0;
    for (const MidiEvent& event : events) {
        if (event.controlsOnly) continue;
        const auto at = std::clamp(static_cast<std::size_t>(std::max(event.sampleOffset, 0)), done, total);
        if (at > done) render(left.subspan(done, at - done), right.subspan(done, at - done));
        done = at;
        handle(event);
    }
    if (total > done) render(left.subspan(done, total - done), right.subspan(done, total - done));
}

void Synth::handle(const MidiEvent& event) noexcept
{
    const int kind = event.status & 0xF0;
    if (kind == 0x90 && event.data2 > 0) {
        noteOn(event.data1 & 0x7F, event.data2);
    } else if (kind == 0x80 || kind == 0x90) {
        noteOff(event.data1 & 0x7F);
    } else if (kind == 0xB0) {
        switch (event.data1) {
        case 64: setSustain(event.data2 >= 64); break;
        case 120: releaseAll(true); break; // all sound off
        case 121: setSustain(false); break;
        case 123: releaseAll(false); break; // all notes off
        default: break;
        }
    }
}

void Synth::noteOn(int note, int velocity) noexcept
{
    const float vel = 1.0F - (m_d.velocity * (1.0F - (static_cast<float>(velocity) / 127.0F)));
    // The same note again: its voice plays it again from where it is (no click).
    auto same = std::ranges::find_if(m_voices, [&](const Voice& v) { return v.note == note && v.active() && !v.stolen; });
    Voice* target = same != m_voices.end() ? &*same : nullptr;
    if (target == nullptr) {
        const auto sounding = [](const Voice& v) { return v.active() && !v.stolen; };
        if (std::ranges::count_if(m_voices, sounding) >= kMaxNotes) {
            // Too many: the oldest (one let go first) fades out fast.
            Voice* oldest = nullptr;
            for (Voice& v : m_voices) {
                if (!sounding(v)) continue;
                const bool better = oldest == nullptr || (oldest->keyDown && !v.keyDown) || (oldest->keyDown == v.keyDown && v.order < oldest->order);
                if (better) oldest = &v;
            }
            if (oldest != nullptr) {
                oldest->stolen = true;
                oldest->amp.fastRelease(m_rate);
            }
        }
        const auto free = std::ranges::find_if(m_voices, [](const Voice& v) { return !v.active(); });
        if (free == m_voices.end()) return; // every slot fading out: this note waits for none
        target = &*free;
        *target = Voice{};
        target->note = note;
        // Each saw starts somewhere of its own (no phasey attack), each drift its own way.
        std::ranges::generate(target->sawPhase, [this] { return m_random.unit(); });
        std::ranges::generate(target->octavePhase, [this] { return m_random.unit(); });
        target->pulsePhase = m_random.unit();
        target->pwmPhase = m_random.unit();
        target->driftNow = m_random.bipolar() * static_cast<float>(m_d.type->driftCents);
        target->driftTarget = target->driftNow;
        target->amp.set(m_d.attack, 0.2, 1.0, m_d.release, m_rate);
    }
    target->velocity = vel;
    target->keyDown = true;
    target->sustained = false;
    target->order = ++m_order;
    target->amp.gateOn();
}

void Synth::noteOff(int note) noexcept
{
    for (Voice& v : m_voices) {
        if (v.note != note || !v.keyDown || v.stolen) continue;
        v.keyDown = false;
        if (m_sustain) {
            v.sustained = true;
        } else {
            v.amp.gateOff();
        }
    }
}

void Synth::setSustain(bool down) noexcept
{
    m_sustain = down;
    if (down) return;
    for (Voice& v : m_voices) {
        if (v.sustained) {
            v.sustained = false;
            v.amp.gateOff();
        }
    }
}

void Synth::releaseAll(bool fast) noexcept
{
    m_sustain = false;
    for (Voice& v : m_voices) {
        if (!v.active()) continue;
        v.keyDown = false;
        v.sustained = false;
        if (fast) v.amp.fastRelease(m_rate);
        else v.amp.gateOff();
    }
}

bool Synth::holdsNotes() const noexcept
{
    return std::ranges::any_of(m_voices, [](const Voice& v) { return v.active(); });
}

int Synth::soundingNotes() const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_voices, [](const Voice& v) { return v.active(); }));
}

void Synth::renderVoice(Voice& v, std::span<float> left, std::span<float> right) noexcept
{
    const PadType& t = *m_d.type;
    const auto rate = static_cast<float>(m_rate);
    const double baseHz = 440.0 * std::exp2((v.note - 69) / 12.0);
    const auto motion = static_cast<float>(m_d.motion);
    const float driftGlide = 1.0F / (0.08F * rate); // the drift glides to each new target (most of the way in ~a quarter second)
    const float pwmStep = static_cast<float>(t.pwmHz) / rate;
    const float vibratoStep = static_cast<float>(t.vibratoHz) / rate;
    auto outRight = right.begin();
    for (float& outLeft : left) {
        // Slow random drift, as old oscillators wander: a new target every half second or so.
        if (--v.driftCountdown <= 0) {
            v.driftTarget = m_random.bipolar() * static_cast<float>(t.driftCents) * motion;
            v.driftCountdown = static_cast<int>(rate * (0.4F + 0.4F * m_random.unit()));
        }
        v.driftNow += (v.driftTarget - v.driftNow) * driftGlide;
        v.age += 1.0F / rate;
        v.vibratoPhase += vibratoStep;
        if (v.vibratoPhase >= 1.0F) v.vibratoPhase -= 1.0F;
        const float vibratoIn = std::clamp((v.age - static_cast<float>(m_d.attack) * 0.5F) / 1.5F, 0.0F, 1.0F);
        const float vibrato = static_cast<float>(t.vibratoCents) * vibratoIn * std::sin(2.0F * std::numbers::pi_v<float> * v.vibratoPhase);
        const double hz = baseHz * static_cast<double>(centsRatio(static_cast<double>(v.driftNow + vibrato)));
        const auto dt = static_cast<float>(hz / m_rate);

        float l = 0.0F;
        float r = 0.0F;
        if (m_d.sawGain > 0.0F) {
            for (std::size_t s = 0; s < kSaws; ++s) {
                if (m_d.sawLeft.at(s) == 0.0F && m_d.sawRight.at(s) == 0.0F) continue;
                const float step = dt * centsRatio(m_d.sawOffset.at(s));
                float& phase = v.sawPhase.at(s);
                const float x = sawstack::oscillator(sawstack::Wave::Saw, phase, step) * m_d.sawGain;
                phase += step;
                if (phase >= 1.0F) phase -= 1.0F;
                l += x * m_d.sawLeft.at(s);
                r += x * m_d.sawRight.at(s);
            }
        }
        if (m_d.octaveGain > 0.0F) {
            for (std::size_t s = 0; s < kSaws; ++s) {
                if (m_d.sawLeft.at(s) == 0.0F && m_d.sawRight.at(s) == 0.0F) continue;
                const float step = 2.0F * dt * centsRatio(-m_d.sawOffset.at(s));
                float& phase = v.octavePhase.at(s);
                const float x = sawstack::oscillator(sawstack::Wave::Saw, phase, step) * m_d.octaveGain;
                phase += step;
                if (phase >= 1.0F) phase -= 1.0F;
                l += x * m_d.sawRight.at(s); // (mirrored: the octave opposite its saw)
                r += x * m_d.sawLeft.at(s);
            }
        }
        float centre = 0.0F;
        if (m_d.pulseGain > 0.0F) {
            v.pwmPhase += pwmStep;
            if (v.pwmPhase >= 1.0F) v.pwmPhase -= 1.0F;
            const float width = std::clamp(static_cast<float>(t.pulseWidth + t.pwmDepth * motion * std::sin(2.0 * std::numbers::pi * v.pwmPhase)), 0.05F, 0.95F);
            // A pulse: a saw minus the same saw shifted by the width (both corrected).
            float shifted = v.pulsePhase + width;
            if (shifted >= 1.0F) shifted -= 1.0F;
            const float a = 2.0F * v.pulsePhase - 1.0F - sawstack::polyBlep(v.pulsePhase, dt);
            const float b = 2.0F * shifted - 1.0F - sawstack::polyBlep(shifted, dt);
            centre += (a - b) * 0.5F * m_d.pulseGain;
            v.pulsePhase += dt;
            if (v.pulsePhase >= 1.0F) v.pulsePhase -= 1.0F;
        }
        if (m_d.subGain > 0.0F) {
            const float step = dt * 0.5F;
            centre += sawstack::oscillator(t.subSine ? sawstack::Wave::Sine : sawstack::Wave::Square, v.subPhase, step) * m_d.subGain;
            v.subPhase += step;
            if (v.subPhase >= 1.0F) v.subPhase -= 1.0F;
        }
        for (std::size_t b = 0; b < kDrawbars; ++b) {
            if (m_d.drawbarGain.at(b) <= 0.0F) continue;
            float& phase = v.organPhase.at(b);
            centre += std::sin(2.0F * std::numbers::pi_v<float> * phase) * m_d.drawbarGain.at(b);
            phase += dt * static_cast<float>(kDrawbarRatios.at(b));
            if (phase >= 1.0F) phase -= 1.0F;
        }
        if (m_d.noiseGain > 0.0F) {
            l += m_random.bipolar() * m_d.noiseGain;
            r += m_random.bipolar() * m_d.noiseGain;
        }
        l += centre;
        r += centre;

        // The filter: the pad's cutoff, the Tone knob, higher notes a little brighter, the slow breath.
        if (--v.filterCountdown <= 0) {
            v.filterCountdown = kFilterUpdate;
            const double lfo = std::sin(2.0 * std::numbers::pi * m_lfoPhase) * t.filterLfoOctaves * m_d.motion;
            const double cutoff = m_d.cutoffHz * std::exp2((t.keyTrack * (v.note - 60) / 12.0) + lfo);
            v.lpL.set(cutoff, t.resonance, m_rate);
            v.lpR.set(cutoff, t.resonance, m_rate);
            if (t.choir && v.vowelL.at(0).g == 0.0F) {
                for (std::size_t f = 0; f < 3; ++f) {
                    v.vowelL.at(f).set(kVowelHz.at(f), kVowelQ.at(f), m_rate);
                    v.vowelR.at(f).set(kVowelHz.at(f), kVowelQ.at(f), m_rate);
                }
            }
        }
        l = v.lpL.lowPass(l);
        r = v.lpR.lowPass(r);
        if (t.choir) {
            // The vowel: three resonances in parallel (a band-pass is what the low- and high-passes leave).
            float vl = 0.0F;
            float vr = 0.0F;
            for (std::size_t f = 0; f < 3; ++f) {
                float low = 0.0F;
                float high = 0.0F;
                v.vowelL.at(f).tick(l, low, high);
                vl += (l - low - high) / v.vowelL.at(f).k * kVowelGain.at(f);
                v.vowelR.at(f).tick(r, low, high);
                vr += (r - low - high) / v.vowelR.at(f).k * kVowelGain.at(f);
            }
            l = vl * 2.0F;
            r = vr * 2.0F;
        }
        const float level = v.amp.tick() * v.velocity;
        outLeft += l * level;
        *outRight++ += r * level;
        if (!v.amp.active()) break;
    }
    if (!v.amp.active()) {
        v.note = -1;
        v.stolen = false;
    }
}

void Synth::render(std::span<float> left, std::span<float> right) noexcept
{
    if (m_d.type == nullptr) return;
    for (Voice& v : m_voices) {
        if (v.active()) renderVoice(v, left, right);
    }
    m_lfoPhase += static_cast<double>(left.size()) * m_d.type->filterLfoHz / m_rate;
    if (m_lfoPhase >= 1.0) m_lfoPhase -= std::floor(m_lfoPhase);
    const float target = m_d.masterGain;
    const float smooth = 1.0F - std::exp(-1.0F / (0.02F * static_cast<float>(m_rate)));
    auto outRight = right.begin();
    for (float& outLeft : left) {
        float l = m_lowCutL.highPass(outLeft);
        float r = m_lowCutR.highPass(*outRight);
        m_chorus.tick(l, r);
        if (m_ensembleMix > 0.0F) {
            // A string machine's ensemble: three moving delays, a slow and a fast sweep, 120° apart.
            m_ensemblePhase += 1.0 / m_rate;
            m_ensembleL.push(l);
            m_ensembleR.push(r);
            float el = 0.0F;
            float er = 0.0F;
            for (int k = 0; k < 3; ++k) {
                const double third = k / 3.0;
                const double slow = std::sin(2.0 * std::numbers::pi * (0.6 * m_ensemblePhase + third));
                const double fast = std::sin(2.0 * std::numbers::pi * (6.0 * m_ensemblePhase + third));
                const auto delay = static_cast<float>((0.005 + 0.0015 * slow + 0.0003 * fast) * m_rate);
                el += m_ensembleL.read(delay);
                er += m_ensembleR.read(delay * 1.07F);
            }
            l = l * (1.0F - m_ensembleMix * 0.5F) + el / 3.0F * m_ensembleMix;
            r = r * (1.0F - m_ensembleMix * 0.5F) + er / 3.0F * m_ensembleMix;
        }
        m_width.tick(l, r);
        m_reverb.tick(l, r);
        m_gainNow += (target - m_gainNow) * smooth;
        // Soft at the very top: never a hard clip, however many notes.
        outLeft = std::tanh(l * m_gainNow);
        *outRight++ = std::tanh(r * m_gainNow);
    }
    if (m_ensemblePhase > 1000.0) m_ensemblePhase -= 1000.0;
}

} // namespace gigchain::engine::pads
