#include "SoundFont.h"

#include "EngineLog.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <fluidsynth.h>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace gigchain::engine::sf2 {

namespace {

// ---------------------------------------------------------------- FluidSynth's messages
// Logged on the main and reading threads; on the audio thread (which must not
// log) only counted. While a file is read, its errors are also kept, to say why.
// (FluidSynth's log callback takes no state of ours per call: these say which thread it is on.)
thread_local QString* t_capture = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): this thread's read, collecting its errors
thread_local bool t_audioThread = false;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): set while this thread plays
thread_local bool t_quiet = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): a synth made or taken down: "no preset found" until it has its font, not news
std::atomic<uint64_t> g_audioMessages{0}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): counted on the audio thread

void onFluidLog(int level, const char* message, void* /*data*/)
{
    if (t_audioThread) {
        g_audioMessages.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const QString text = QString::fromUtf8(message).trimmed();
    if (level <= FLUID_ERR) {
        qCWarning(lcEngine).noquote() << "FluidSynth:" << text;
        if (t_capture != nullptr) {
            if (!t_capture->isEmpty()) t_capture->append(u"; "_s);
            t_capture->append(text);
        }
    } else if (!t_quiet) {
        qCInfo(lcEngine).noquote() << "FluidSynth:" << text;
    }
}

struct QuietScope
{
    QuietScope() { t_quiet = true; }
    ~QuietScope() { t_quiet = false; }
    QuietScope(const QuietScope&) = delete;
    QuietScope& operator=(const QuietScope&) = delete;
    QuietScope(QuietScope&&) = delete;
    QuietScope& operator=(QuietScope&&) = delete;
};

void installLog()
{
    static std::once_flag once;
    std::call_once(once, [] {
        fluid_set_log_function(FLUID_PANIC, onFluidLog, nullptr);
        fluid_set_log_function(FLUID_ERR, onFluidLog, nullptr);
        fluid_set_log_function(FLUID_WARN, onFluidLog, nullptr);
        fluid_set_log_function(FLUID_INFO, nullptr, nullptr);
        fluid_set_log_function(FLUID_DBG, nullptr, nullptr);
    });
}

// Marks this thread as the audio thread while it lives (FluidSynth's messages are only counted).
struct AudioThreadScope
{
    AudioThreadScope() { t_audioThread = true; }
    ~AudioThreadScope() { t_audioThread = false; }
    AudioThreadScope(const AudioThreadScope&) = delete;
    AudioThreadScope& operator=(const AudioThreadScope&) = delete;
    AudioThreadScope(AudioThreadScope&&) = delete;
    AudioThreadScope& operator=(AudioThreadScope&&) = delete;
};

// A synth's settings: what every SoundFont Player synth shares.
fluid_settings_t* newSettings(double sampleRate, int polyphony, bool effects)
{
    fluid_settings_t* s = new_fluid_settings();
    if (s == nullptr) return nullptr;
    fluid_settings_setnum(s, "synth.sample-rate", sampleRate);
    fluid_settings_setint(s, "synth.polyphony", polyphony);
    fluid_settings_setint(s, "synth.threadsafe-api", 0); // one thread at a time uses a synth (see Synth)
    fluid_settings_setint(s, "synth.cpu-cores", 1);
    fluid_settings_setint(s, "synth.lock-memory", 0);
    fluid_settings_setint(s, "synth.dynamic-sample-loading", 0); // every sample in memory: no disk on the audio thread
    fluid_settings_setint(s, "synth.reverb.active", effects ? 1 : 0);
    fluid_settings_setint(s, "synth.chorus.active", effects ? 1 : 0);
    fluid_settings_setnum(s, "synth.gain", 0.6); // FluidSynth's 0.2 is ~10 dB under the app's other instruments
    return s;
}

double timecents(double seconds)
{
    return 1200.0 * std::log2(std::max(seconds, 0.001));
}
double absoluteCents(double hz)
{
    return 1200.0 * std::log2(hz / 8.176); // SF2.01 8.1.3: cents above 8.176 Hz
}

// The defaults FluidSynth's effects start at (FLUID_REVERB_DEFAULT_LEVEL, FLUID_CHORUS_DEFAULT_LEVEL).
constexpr double kReverbDefault = 0.7;
constexpr double kChorusDefault = 0.6;

// The largest polyphony a player plays (FluidSynth's default).
constexpr int kPolyphony = 256;

} // namespace

// ---------------------------------------------------------------- the controls
QString paramName(uint32_t id)
{
    switch (static_cast<Param>(id)) {
    case Param::Preset: return u"Preset"_s;
    case Param::ReverbOn: return u"Reverb"_s;
    case Param::ReverbLevel: return u"Reverb Level"_s;
    case Param::ChorusOn: return u"Chorus"_s;
    case Param::ChorusLevel: return u"Chorus Level"_s;
    case Param::Attack: return u"Attack"_s;
    case Param::Decay: return u"Decay"_s;
    case Param::Sustain: return u"Sustain"_s;
    case Param::Release: return u"Release"_s;
    case Param::LfoDelay: return u"LFO Delay"_s;
    case Param::LfoAmount: return u"LFO Amount"_s;
    case Param::LfoSpeed: return u"LFO Speed"_s;
    case Param::Cutoff: return u"Cutoff"_s;
    case Param::CutoffMod: return u"Cutoff Mod"_s;
    case Param::Quality: return u"Quality"_s;
    case Param::Mode: return u"Mode"_s;
    case Param::VelocityCutoff: return u"Velocity Cutoff"_s;
    case Param::Count: break;
    }
    return u"Parameter %1"_s.arg(id);
}

QString paramKey(uint32_t id)
{
    switch (static_cast<Param>(id)) {
    case Param::Preset: return u"preset"_s;
    case Param::ReverbOn: return u"reverbOn"_s;
    case Param::ReverbLevel: return u"reverbLevel"_s;
    case Param::ChorusOn: return u"chorusOn"_s;
    case Param::ChorusLevel: return u"chorusLevel"_s;
    case Param::Attack: return u"attack"_s;
    case Param::Decay: return u"decay"_s;
    case Param::Sustain: return u"sustain"_s;
    case Param::Release: return u"release"_s;
    case Param::LfoDelay: return u"lfoDelay"_s;
    case Param::LfoAmount: return u"lfoAmount"_s;
    case Param::LfoSpeed: return u"lfoSpeed"_s;
    case Param::Cutoff: return u"cutoff"_s;
    case Param::CutoffMod: return u"cutoffMod"_s;
    case Param::Quality: return u"quality"_s;
    case Param::Mode: return u"mode"_s;
    case Param::VelocityCutoff: return u"velocityCutoff"_s;
    case Param::Count: break;
    }
    return u"p%1"_s.arg(id);
}

double paramDefault(uint32_t id)
{
    switch (static_cast<Param>(id)) {
    case Param::ReverbOn:
    case Param::ChorusOn: return 1.0;
    case Param::ReverbLevel:
    case Param::ChorusLevel: return 0.5; // as the SoundFont's author heard it
    case Param::Quality: return stepValue(static_cast<int>(Quality::Standard), kQualities);
    case Param::Mode: return stepValue(static_cast<int>(Mode::Auto), kModes);
    case Param::VelocityCutoff: return 1.0;
    default: return 0.0; // every knob and slider fully left: the SoundFont's own values
    }
}

QStringList paramChoices(uint32_t id)
{
    switch (static_cast<Param>(id)) {
    case Param::Quality: return {u"Crunchy"_s, u"Standard"_s, u"HQ"_s};
    case Param::Mode: return {u"Auto"_s, u"Instrument"_s, u"Drums"_s};
    case Param::VelocityCutoff:
    case Param::ReverbOn:
    case Param::ChorusOn: return {u"Off"_s, u"On"_s};
    default: return {};
    }
}

int stepOf(double value, int count)
{
    if (count <= 1 || !std::isfinite(value)) return 0;
    return std::clamp(static_cast<int>(std::lround(value * (count - 1))), 0, count - 1);
}

double stepValue(int step, int count)
{
    return count <= 1 ? 0.0 : std::clamp(step, 0, count - 1) / static_cast<double>(count - 1);
}

double attackSeconds(double v) { return 0.001 * std::pow(10000.0, std::clamp(v, 0.0, 1.0)); }
double decaySeconds(double v) { return 0.001 * std::pow(20000.0, std::clamp(v, 0.0, 1.0)); }
double sustainDb(double v) { return -60.0 * (1.0 - std::clamp(v, 0.0, 1.0)); }
double lfoDelaySeconds(double v) { return 0.001 * std::pow(5000.0, std::clamp(v, 0.0, 1.0)); }
double lfoCents(double v)
{
    const double d = (std::clamp(v, 0.0, 1.0) - 0.5) * 2.0; // -1 .. 1, 0 at 12 o'clock
    return std::copysign(d * d, d) * 200.0;
}
double lfoHz(double v) { return 0.1 * std::pow(200.0, std::clamp(v, 0.0, 1.0)); }
double cutoffHz(double v) { return 20.0 * std::pow(1000.0, std::clamp(v, 0.0, 1.0)); }
double cutoffModCents(double v) { return 9600.0 * std::clamp(v, 0.0, 1.0); }
double effectLevel(double v) { return 2.0 * std::clamp(v, 0.0, 1.0); }

std::vector<Override> overrides(const std::array<double, kParamCount>& values)
{
    std::vector<Override> list;
    list.reserve(kMaxOverrides);
    overridesInto(values, list);
    return list;
}

void overridesInto(const std::array<double, kParamCount>& values, std::vector<Override>& list)
{
    list.clear();
    const auto at = [&values](Param p) { return values.at(static_cast<std::size_t>(p)); };
    const auto add = [&list](int generator, double value) { list.push_back({.generator = generator, .value = static_cast<float>(value)}); };
    // Each at its minimum: the SoundFont's own value (no override).
    if (at(Param::Attack) > 0.0) add(GEN_VOLENVATTACK, timecents(attackSeconds(at(Param::Attack))));
    if (at(Param::Decay) > 0.0) add(GEN_VOLENVDECAY, timecents(decaySeconds(at(Param::Decay))));
    if (at(Param::Sustain) > 0.0) add(GEN_VOLENVSUSTAIN, -10.0 * sustainDb(at(Param::Sustain))); // centibels down
    if (at(Param::Release) > 0.0) add(GEN_VOLENVRELEASE, timecents(decaySeconds(at(Param::Release))));
    if (at(Param::LfoDelay) > 0.0) add(GEN_VIBLFODELAY, timecents(lfoDelaySeconds(at(Param::LfoDelay))));
    if (at(Param::LfoAmount) > 0.0) add(GEN_VIBLFOTOPITCH, lfoCents(at(Param::LfoAmount)));
    if (at(Param::LfoSpeed) > 0.0) add(GEN_VIBLFOFREQ, absoluteCents(lfoHz(at(Param::LfoSpeed))));
    if (at(Param::Cutoff) > 0.0) add(GEN_FILTERFC, absoluteCents(cutoffHz(at(Param::Cutoff))));
    if (at(Param::CutoffMod) > 0.0) add(GEN_MODENVTOFILTERFC, cutoffModCents(at(Param::CutoffMod)));
}

// ---------------------------------------------------------------- the files
Font::~Font()
{
    // FluidSynth frees the font with the synth that read it. No player
    // holds it any more (each holds the Font while it plays).
    if (m_owner != nullptr) delete_fluid_synth(m_owner);
    if (m_settings != nullptr) delete_fluid_settings(m_settings);
}

void Font::load()
{
    const auto fail = [this](const QString& why) {
        m_problem = why;
        qCWarning(lcEngine).noquote() << "The SoundFont" << m_path << "could not be read:" << why;
        m_state.store(State::Failed, std::memory_order_release);
    };
    const QFileInfo info(m_path);
    if (!info.exists()) return fail(u"the file is not there"_s);
    if (!info.isFile() || !info.isReadable()) return fail(u"the file cannot be opened"_s);

    installLog();
    const QuietScope quiet; // (its errors are still logged, and kept to say why)
    // A synth of one voice, no effects: it only reads and keeps the font.
    m_settings = newSettings(44100.0, 1, false);
    if (m_settings != nullptr) m_owner = new_fluid_synth(m_settings);
    if (m_owner == nullptr) return fail(u"FluidSynth could not start"_s);

    QString errors;
    t_capture = &errors;
    const QByteArray name = QFile::encodeName(m_path);
    const int id = fluid_synth_sfload(m_owner, name.constData(), 0);
    t_capture = nullptr;
    if (id == FLUID_FAILED) {
        // FluidSynth's own words (logged as they came) are for the log; the player is told what it means.
        // (onFluidLog filled `errors` through t_capture: cppcheck cannot see that.)
        // cppcheck-suppress knownConditionTrueFalse
        qCWarning(lcEngine).noquote() << "FluidSynth on" << m_path << ":" << (errors.isEmpty() ? u"no reason given"_s : errors);
        return fail(u"it is not a SoundFont 2 file, or it is damaged"_s);
    }
    m_sfont = fluid_synth_get_sfont_by_id(m_owner, id);
    if (m_sfont == nullptr) return fail(u"FluidSynth lost it after reading it"_s);

    fluid_sfont_iteration_start(m_sfont);
    while (fluid_preset_t* preset = fluid_sfont_iteration_next(m_sfont)) {
        m_presets.push_back({.bank = fluid_preset_get_banknum(preset),
                             .program = fluid_preset_get_num(preset),
                             .name = QString::fromUtf8(fluid_preset_get_name(preset)).trimmed()});
    }
    if (m_presets.empty()) return fail(u"it has no presets"_s);
    std::ranges::sort(m_presets, [](const PresetInfo& a, const PresetInfo& b) {
        return std::tie(a.bank, a.program) < std::tie(b.bank, b.program);
    });
    qCInfo(lcEngine).noquote() << "Read the SoundFont" << m_path << "(" << m_presets.size() << "presets )";
    m_state.store(State::Ready, std::memory_order_release);
}

FontLibrary& FontLibrary::instance()
{
    static FontLibrary library;
    return library;
}

FontLibrary::FontLibrary()
{
    // Reads stop before the app goes (as the pianos').
    if (auto* app = QCoreApplication::instance(); app != nullptr) {
        QObject::connect(app, &QCoreApplication::aboutToQuit, app, [this] { shutdown(); });
    }
}

FontLibrary::~FontLibrary()
{
    shutdown();
}

std::shared_ptr<Font> FontLibrary::request(const QString& path)
{
    const QString key = QFileInfo(path).absoluteFilePath();
    const std::scoped_lock lock(m_mutex);
    if (m_shutDown) return nullptr;
    if (auto it = m_fonts.find(key); it != m_fonts.end()) {
        // (One that could not be read is read again: the file may be back, or mended.)
        if (auto font = it->second.lock(); font && font->state() != Font::State::Failed) return font;
    }
    // (Font's constructor is private: make_shared cannot reach it.)
    std::shared_ptr<Font> font(new Font(key)); // NOLINT(cppcoreguidelines-owning-memory)
    m_fonts[key] = font;
    // Readers that are done are joined (at once: they have finished); the
    // ones still reading are left to it.
    std::erase_if(m_readers, [](Reader& r) {
        if (!r.done->load(std::memory_order_acquire)) return false;
        r.thread.join();
        return true;
    });
    // The reader holds the font while it reads: a player letting it go meanwhile frees nothing it uses.
    auto done = std::make_shared<std::atomic<bool>>(false);
    m_readers.push_back({.thread = std::thread([font, done]() mutable {
                             font->load();
                             font.reset(); // (the last holder frees it here, before saying it is done)
                             done->store(true, std::memory_order_release);
                         }),
                         .done = done});
    // Forget the ones nobody holds.
    std::erase_if(m_fonts, [](const auto& entry) { return entry.second.expired(); });
    return font;
}

void FontLibrary::shutdown()
{
    std::vector<Reader> readers;
    {
        const std::scoped_lock lock(m_mutex);
        m_shutDown = true;
        readers.swap(m_readers);
    }
    for (Reader& r : readers) {
        if (r.thread.joinable()) r.thread.join();
    }
}

// ---------------------------------------------------------------- the synth
core::Result<std::unique_ptr<Synth>> Synth::create(std::shared_ptr<Font> font, double sampleRate, bool velocityCutoff)
{
    const auto fail = [](const QString& why) {
        qCWarning(lcEngine).noquote() << "The SoundFont Player could not start:" << why;
        return core::fail(core::ErrorCode::InvalidData, u"The SoundFont Player could not start (%1)"_s.arg(why));
    };
    if (!font || font->state() != Font::State::Ready || font->sfont() == nullptr) return fail(u"its SoundFont is not read"_s);
    installLog();
    const QuietScope quiet;
    std::unique_ptr<Synth> s(new Synth()); // NOLINT(cppcoreguidelines-owning-memory) (private constructor)
    s->m_rate = sampleRate;
    s->m_velocityCutoff = velocityCutoff;
    s->m_settings = newSettings(sampleRate, kPolyphony, true);
    if (s->m_settings != nullptr) s->m_synth = new_fluid_synth(s->m_settings);
    if (s->m_synth == nullptr) return fail(u"FluidSynth could not make a synth"_s);
    if (fluid_synth_add_sfont(s->m_synth, font->sfont()) == FLUID_FAILED) return fail(u"FluidSynth would not take the SoundFont"_s);
    s->m_font = std::move(font);
    if (velocityCutoff) {
        // The SoundFont standard's velocity-to-cutoff (SF2.04 8.4.2: softer
        // notes darker, up to 2 octaves). FluidSynth keeps its own at zero
        // ("to stop forcing velocity based filtering"), so it is added here,
        // as FL's option switches it.
        fluid_mod_t* mod = new_fluid_mod();
        fluid_mod_set_source1(mod, FLUID_MOD_VELOCITY, FLUID_MOD_GC | FLUID_MOD_LINEAR | FLUID_MOD_UNIPOLAR | FLUID_MOD_NEGATIVE);
        fluid_mod_set_source2(mod, FLUID_MOD_NONE, FLUID_MOD_GC | FLUID_MOD_LINEAR | FLUID_MOD_UNIPOLAR | FLUID_MOD_POSITIVE);
        fluid_mod_set_dest(mod, GEN_FILTERFC);
        fluid_mod_set_amount(mod, -2400.0);
        if (fluid_synth_add_default_mod(s->m_synth, mod, FLUID_SYNTH_ADD) == FLUID_FAILED) {
            qCWarning(lcEngine) << "The SoundFont Player could not switch on velocity-to-cutoff";
        }
        delete_fluid_mod(mod);
    }
    s->m_overrides.reserve(kMaxOverrides);
    s->m_values.fill(-1.0); // everything applies on the first block
    return s;
}

Synth::~Synth()
{
    const QuietScope quiet;
    if (m_synth != nullptr) {
        // The font is the reader's (Font): only taken off this synth.
        if (m_font && m_font->sfont() != nullptr) fluid_synth_remove_sfont(m_synth, m_font->sfont());
        delete_fluid_synth(m_synth);
    }
    if (m_settings != nullptr) delete_fluid_settings(m_settings);
}

bool Synth::drums() const
{
    switch (static_cast<Mode>(stepOf(m_values.at(static_cast<std::size_t>(Param::Mode)), kModes))) {
    case Mode::Drums: return true;
    case Mode::Instrument: return false;
    case Mode::Auto: break;
    }
    const auto& presets = m_font->presets();
    return m_preset >= 0 && std::cmp_less(m_preset, presets.size()) && presets.at(static_cast<std::size_t>(m_preset)).bank == 128;
}

void Synth::apply(const std::array<double, kParamCount>& values)
{
    if (values == m_values) return;
    const auto changed = [&](Param p) {
        const auto i = static_cast<std::size_t>(p);
        return values.at(i) != m_values.at(i);
    };
    const auto at = [&values](Param p) { return values.at(static_cast<std::size_t>(p)); };

    if (changed(Param::Preset)) {
        const auto& presets = m_font->presets();
        const int index = stepOf(at(Param::Preset), static_cast<int>(presets.size()));
        if (index != m_preset && !presets.empty()) {
            const PresetInfo& p = presets.at(static_cast<std::size_t>(index));
            m_preset = index;
            fluid_synth_program_select_by_sfont_name(m_synth, 0, fluid_sfont_get_name(m_font->sfont()), p.bank, p.program);
        }
    }
    if (changed(Param::ReverbOn)) fluid_synth_reverb_on(m_synth, -1, at(Param::ReverbOn) >= 0.5 ? 1 : 0);
    if (changed(Param::ReverbLevel)) {
        fluid_synth_set_reverb_group_level(m_synth, -1, std::min(1.0, kReverbDefault * effectLevel(at(Param::ReverbLevel))));
    }
    if (changed(Param::ChorusOn)) fluid_synth_chorus_on(m_synth, -1, at(Param::ChorusOn) >= 0.5 ? 1 : 0);
    if (changed(Param::ChorusLevel)) {
        fluid_synth_set_chorus_group_level(m_synth, -1, kChorusDefault * effectLevel(at(Param::ChorusLevel)));
    }
    if (changed(Param::Quality)) {
        static constexpr std::array<int, kQualities> kInterp{FLUID_INTERP_LINEAR, FLUID_INTERP_4THORDER, FLUID_INTERP_7THORDER};
        fluid_synth_set_interp_method(m_synth, -1, kInterp.at(static_cast<std::size_t>(stepOf(at(Param::Quality), kQualities))));
    }

    // The sound's overrides: on the notes playing now, as on every new one.
    bool overridesChanged = false;
    for (const Param p : {Param::Attack, Param::Decay, Param::Sustain, Param::Release, Param::LfoDelay, Param::LfoAmount,
                          Param::LfoSpeed, Param::Cutoff, Param::CutoffMod}) {
        overridesChanged = overridesChanged || changed(p);
    }
    m_values = values;
    if (overridesChanged) {
        overridesInto(values, m_overrides); // (reserved for all of them: no allocation here)
        overrideVoices(false);
    }
}

void Synth::overrideVoices(bool newOnly)
{
    std::array<fluid_voice_t*, kPolyphony> voices{};
    fluid_synth_get_voicelist(m_synth, voices.data(), static_cast<int>(voices.size()), -1);
    const bool oneShots = newOnly && drums();
    long long newest = m_lastVoiceId;
    for (fluid_voice_t* v : voices) {
        if (v == nullptr) break;
        const auto id = static_cast<long long>(fluid_voice_get_id(v));
        if (newOnly && id <= m_lastVoiceId) continue;
        newest = std::max(newest, id);
        for (const Override& o : m_overrides) fluid_voice_gen_set(v, o.generator, o.value);
        for (const Override& o : m_overrides) fluid_voice_update_param(v, o.generator); // (all set first: some stages read others)
        if (oneShots) {
            // Drums: each hit plays its sample to the end (note-off is ignored).
            fluid_voice_gen_set(v, GEN_SAMPLEMODE, 0.0F);
            fluid_voice_update_param(v, GEN_SAMPLEMODE);
        }
    }
    if (newOnly) m_lastVoiceId = newest;
    // FluidSynth hands a voice's changes to its renderer only when one of its
    // own calls ends (fluid_synth_api_exit flushes them); these were made
    // directly on the voices. Without this they would land a block late: the
    // note's first block played on the SoundFont's envelope, and a short
    // attack override would be missed altogether. Any call does it; this one
    // only reads.
    int unused = 0;
    fluid_synth_get_cc(m_synth, 0, 0, &unused);
}

void Synth::handle(const MidiEvent& e)
{
    const int type = e.status & 0xF0;
    const int d1 = e.data1 & 0x7F;
    const int d2 = e.data2 & 0x7F;
    switch (type) {
    case 0x90:
        if (d2 > 0) {
            fluid_synth_noteon(m_synth, 0, d1, d2);
            overrideVoices(true);
            break;
        }
        [[fallthrough]];
    case 0x80:
        if (!drums()) fluid_synth_noteoff(m_synth, 0, d1);
        break;
    case 0xA0: fluid_synth_key_pressure(m_synth, 0, d1, d2); break;
    case 0xB0:
        // Bank select is the window's (the preset), not the keyboard's.
        if (d1 != 0 && d1 != 32) fluid_synth_cc(m_synth, 0, d1, d2);
        break;
    case 0xD0: fluid_synth_channel_pressure(m_synth, 0, d1); break;
    case 0xE0: fluid_synth_pitch_bend(m_synth, 0, d1 | (d2 << 7)); break;
    default: break; // program changes call up the app's sounds, not this player's presets
    }
}

void Synth::process(std::span<const MidiEvent> events, AudioBlock io)
{
    const AudioThreadScope audio;
    int done = 0;
    const auto renderTo = [&](int end) {
        if (end <= done) return;
        fluid_synth_write_float(m_synth, end - done, io.left, done, 1, io.right, done, 1);
        done = end;
    };
    for (const MidiEvent& e : events) {
        renderTo(std::clamp(e.sampleOffset, 0, io.frames));
        handle(e);
    }
    renderTo(io.frames);
}

void Synth::releaseAll()
{
    const AudioThreadScope audio;
    fluid_synth_cc(m_synth, 0, 64, 0); // the pedal up too
    fluid_synth_all_notes_off(m_synth, -1);
}

bool Synth::sounding() const
{
    return fluid_synth_get_active_voice_count(m_synth) > 0;
}

// ---------------------------------------------------------------- the player
Player::Player()
{
    for (uint32_t id = 0; id < static_cast<uint32_t>(kParamCount); ++id) m_params.at(id).store(paramDefault(id));
}

Player::~Player()
{
    // The node is no longer played (the graph let it go): every synth is this thread's to free.
    delete m_incoming.exchange(nullptr);
    delete m_retired.exchange(nullptr);
    delete m_live;
}

void Player::say(const QString& message)
{
    qCWarning(lcEngine).noquote() << message;
    m_messages << message;
}

void Player::prepare(double sampleRate)
{
    m_rate = sampleRate;
    service();
}

void Player::setFile(const QString& path, int bank, int program)
{
    m_path = path;
    m_want = {bank, program};
    m_failedReported = false;
    m_adopted = nullptr;
    m_font = path.isEmpty() ? nullptr : FontLibrary::instance().request(path);
    if (!path.isEmpty() && !m_font) say(u"The SoundFont %1 was not read: the app is closing"_s.arg(QFileInfo(path).fileName()));
    service();
}

Player::FileState Player::fileState() const
{
    if (!m_font) return m_path.isEmpty() ? FileState::None : FileState::Failed;
    switch (m_font->state()) {
    case Font::State::Loading: return FileState::Loading;
    case Font::State::Ready: return m_buildFailed ? FileState::Failed : FileState::Ready;
    case Font::State::Failed: return FileState::Failed;
    }
    return FileState::Failed;
}

std::vector<PresetInfo> Player::presets() const
{
    if (!m_font || m_font->state() != Font::State::Ready) return {};
    return m_font->presets();
}

std::pair<int, int> Player::chosen() const
{
    const std::vector<PresetInfo> list = presets();
    if (list.empty()) return m_want;
    const PresetInfo& p = list.at(static_cast<std::size_t>(stepOf(param(paramId(Param::Preset)), static_cast<int>(list.size()))));
    return {p.bank, p.program};
}

void Player::choosePreset(int bank, int program)
{
    const std::vector<PresetInfo>& list = m_font->presets();
    auto found = std::ranges::find_if(list, [&](const PresetInfo& p) { return p.bank == bank && p.program == program; });
    if (found == list.end() && bank >= 0) {
        say(u"%1 has no preset %2:%3: its first plays"_s.arg(QFileInfo(m_path).fileName()).arg(bank).arg(program));
    }
    if (found == list.end()) found = list.begin();
    setParam(paramId(Param::Preset), stepValue(static_cast<int>(found - list.begin()), static_cast<int>(list.size())));
}

void Player::service()
{
    delete m_retired.exchange(nullptr);

    if (m_font && m_font->state() == Font::State::Failed && !m_failedReported) {
        m_failedReported = true;
        say(u"The SoundFont %1 could not be read: %2"_s.arg(QFileInfo(m_path).fileName(), m_font->problem()));
    }
    const bool ready = m_font && m_font->state() == Font::State::Ready;
    if (ready && m_adopted != m_font.get()) {
        m_adopted = m_font.get();
        ++m_generation;
        choosePreset(m_want.first, m_want.second);
    }

    // What should play: this font at this rate (another synth when that changes).
    const bool velocityCutoff = param(paramId(Param::VelocityCutoff)) >= 0.5;
    const Font* font = ready ? m_font.get() : nullptr;
    if (font == m_builtFont && m_rate == m_builtRate && velocityCutoff == m_builtVelocityCutoff) return;
    if (font == nullptr && m_builtFont == nullptr) return;
    m_builtFont = font;
    m_builtRate = m_rate;
    m_builtVelocityCutoff = velocityCutoff;
    m_buildFailed = false;
    std::unique_ptr<Synth> next;
    if (font != nullptr && m_rate > 0.0) {
        auto made = Synth::create(m_font, m_rate, velocityCutoff);
        if (!made) {
            m_buildFailed = true;
            say(made.error().message);
        } else {
            next = std::move(*made);
        }
    }
    // Handed over; one not yet taken is replaced. No file (or none could be
    // made): the audio thread lets go of what it plays at its next block.
    delete m_incoming.exchange(next ? next.release() : nullptr);
    if (!m_incoming.load(std::memory_order_relaxed)) m_dropLive.store(true, std::memory_order_release);
}

void Player::setParam(uint32_t id, double value)
{
    if (id >= static_cast<uint32_t>(kParamCount) || !std::isfinite(value)) return;
    m_params.at(id).store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
}

double Player::param(uint32_t id) const
{
    if (id >= static_cast<uint32_t>(kParamCount)) return -1.0;
    return m_params.at(id).load(std::memory_order_relaxed);
}

void Player::process(std::span<const MidiEvent> events, AudioBlock io)
{
    // A new synth (the main thread built it) takes over once the old one can be handed back.
    if (m_retired.load(std::memory_order_acquire) == nullptr) {
        if (Synth* next = m_incoming.exchange(nullptr, std::memory_order_acq_rel); next != nullptr) {
            m_retired.store(m_live, std::memory_order_release);
            m_live = next;
        } else if (m_dropLive.exchange(false, std::memory_order_acq_rel)) {
            m_retired.store(m_live, std::memory_order_release);
            m_live = nullptr;
        }
    }
    std::fill_n(io.left, io.frames, 0.0F);
    std::fill_n(io.right, io.frames, 0.0F);
    if (m_live == nullptr) {
        m_holds.store(false, std::memory_order_relaxed);
        return;
    }
    std::array<double, kParamCount> values{};
    for (std::size_t i = 0; i < values.size(); ++i) values.at(i) = m_params.at(i).load(std::memory_order_relaxed);
    m_live->apply(values);
    if (m_releaseAll.exchange(false, std::memory_order_relaxed)) m_live->releaseAll();
    m_live->process(events, io);
    m_holds.store(m_live->sounding(), std::memory_order_relaxed);
}

} // namespace gigchain::engine::sf2
