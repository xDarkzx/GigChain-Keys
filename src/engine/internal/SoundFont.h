#pragma once

#include "INode.h"
#include "MidiEvent.h"

#include "gigchain/core/Error.h"

#include <QString>
#include <QStringList>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

// FluidSynth's types (fluidsynth/types.h), kept out of every file that includes this one.
struct _fluid_hashtable_t;
struct _fluid_synth_t;
struct _fluid_sfont_t;

namespace gigchain::engine::sf2 {

// The SoundFont Player: plays a SoundFont 2 file (.sf2), as FL Studio's
// SoundFont Player does, on FluidSynth. Its controls are FL's, each over the
// SoundFont's own value for a note (FL: "Envelope 2" is the EMU volume
// envelope, "LFO 2" the EMU vibrato LFO); each at its minimum leaves the
// SoundFont's value as it is ("keep it fully left to use the original patch
// value").

// ---------------------------------------------------------------- the controls
enum class Param : uint8_t {
    Preset,         // stepped: the file's presets, in bank then program order
    ReverbOn,       // the built-in reverb (0 off, 1 on)
    ReverbLevel,    // its level: 0.5 is as the SoundFont has it, 1 twice that
    ChorusOn,
    ChorusLevel,
    Attack,         // Envelope: 0 = the SoundFont's, else 1 ms .. 10 s
    Decay,          // 0 = the SoundFont's, else 1 ms .. 20 s
    Sustain,        // 0 = the SoundFont's, else the level held (-60 dB .. full)
    Release,        // 0 = the SoundFont's, else 1 ms .. 20 s
    LfoDelay,       // LFO (pitch): 0 = the SoundFont's, else 1 ms .. 5 s
    LfoAmount,      // 0 = the SoundFont's; 0.5 none; under it the other way round, over it up to ±2 semitones
    LfoSpeed,       // 0 = the SoundFont's, else 0.1 .. 20 Hz
    Cutoff,         // Misc: low-pass cutoff, 0 = the SoundFont's, else 20 Hz .. 20 kHz
    CutoffMod,      // how far the modulation envelope opens it: 0 = the SoundFont's, else up to 8 octaves
    Quality,        // stepped: Crunchy (linear), Standard (4th order), HQ (7th order)
    Mode,           // stepped: Auto (bank 128 plays drums), Instrument, Drums (one-shots)
    VelocityCutoff, // stepped: Off, On (velocity closes the filter, the SoundFont standard)
    Count
};
inline constexpr int kParamCount = static_cast<int>(Param::Count);
[[nodiscard]] constexpr uint32_t paramId(Param p) { return static_cast<uint32_t>(p); }
[[nodiscard]] QString paramName(uint32_t id); // as players see it ("Attack")
[[nodiscard]] QString paramKey(uint32_t id);  // as the settings save it ("attack")
[[nodiscard]] double paramDefault(uint32_t id);
[[nodiscard]] QStringList paramChoices(uint32_t id); // a stepped one's (not Preset: the file's)

enum class Quality : uint8_t { Crunchy, Standard, High };
enum class Mode : uint8_t { Auto, Instrument, Drums };
inline constexpr int kQualities = 3;
inline constexpr int kModes = 3;
// A stepped value's setting (0-1 evenly over `count`).
[[nodiscard]] int stepOf(double value, int count);
[[nodiscard]] double stepValue(int step, int count);

// What each control sets, in the SoundFont's own units (SF2.01 section 8.1.3):
// nothing when it is at its minimum.
struct Override
{
    int generator = 0;
    float value = 0.0F;
};
// Every override the controls `values` make (indexed by Param), into `out`
// (cleared first; never more than kMaxOverrides, so a vector reserved for
// that many never allocates: the audio thread's way).
inline constexpr std::size_t kMaxOverrides = 16;
void overridesInto(const std::array<double, kParamCount>& values, std::vector<Override>& out);
[[nodiscard]] std::vector<Override> overrides(const std::array<double, kParamCount>& values);
// The single conversions (exposed for the tests and the window's readouts).
[[nodiscard]] double attackSeconds(double v);
[[nodiscard]] double decaySeconds(double v); // (and the release)
[[nodiscard]] double sustainDb(double v);
[[nodiscard]] double lfoDelaySeconds(double v);
[[nodiscard]] double lfoCents(double v); // the vibrato depth, signed
[[nodiscard]] double lfoHz(double v);
[[nodiscard]] double cutoffHz(double v);
[[nodiscard]] double cutoffModCents(double v);
[[nodiscard]] double effectLevel(double v); // reverb, chorus: the multiple of the SoundFont's level

// ---------------------------------------------------------------- the files
struct PresetInfo
{
    int bank = 0;
    int program = 0;
    QString name;
};

// One .sf2 read into memory, shared by every player of it (a General MIDI
// SoundFont is often 30-300 MB: never twice). It is freed when the last
// player lets it go.
class Font
{
public:
    enum class State : uint8_t { Loading, Ready, Failed };
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) = delete;
    Font& operator=(Font&&) = delete;

    [[nodiscard]] State state() const { return m_state.load(std::memory_order_acquire); }
    // Once Ready (else empty / nullptr): its presets (bank, program order) and its FluidSynth font.
    [[nodiscard]] const std::vector<PresetInfo>& presets() const { return m_presets; }
    [[nodiscard]] _fluid_sfont_t* sfont() const { return m_sfont; }
    [[nodiscard]] QString path() const { return m_path; }
    // Once Failed: why.
    [[nodiscard]] QString problem() const { return m_problem; }

private:
    friend class FontLibrary;
    explicit Font(QString path) : m_path(std::move(path)) {}
    void load(); // on its own thread

    QString m_path;
    std::atomic<State> m_state{State::Loading};
    QString m_problem;                   // written before m_state turns Failed
    std::vector<PresetInfo> m_presets;   // written before m_state turns Ready
    _fluid_hashtable_t* m_settings = nullptr;
    _fluid_synth_t* m_owner = nullptr;   // holds the font (FluidSynth frees a font with its synth)
    _fluid_sfont_t* m_sfont = nullptr;
};

// Reads SoundFonts, each on a thread of its own, sharing one read of a file.
class FontLibrary
{
public:
    static FontLibrary& instance();
    ~FontLibrary();
    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;
    FontLibrary(FontLibrary&&) = delete;
    FontLibrary& operator=(FontLibrary&&) = delete;

    // Main thread. The file at `path`, read (or being read) once for everyone;
    // nullptr once the app is quitting.
    std::shared_ptr<Font> request(const QString& path);
    // Waits for every read under way (the app quitting); none starts after.
    void shutdown();

private:
    FontLibrary();
    struct Reader
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done; // its read is over (the thread can be joined at once)
    };
    std::mutex m_mutex;
    std::map<QString, std::weak_ptr<Font>> m_fonts;
    std::vector<Reader> m_readers;
    bool m_shutDown = false;
};

// ---------------------------------------------------------------- the player
// One SoundFont Player's sound: a FluidSynth synth playing a Font. Built on
// the main thread, played on the audio thread only, freed on the main thread.
class Synth
{
public:
    // Main thread. Fails with the cause (and logs it).
    static core::Result<std::unique_ptr<Synth>> create(std::shared_ptr<Font> font, double sampleRate, bool velocityCutoff);
    ~Synth();
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;
    Synth(Synth&&) = delete;
    Synth& operator=(Synth&&) = delete;

    [[nodiscard]] const std::shared_ptr<Font>& font() const { return m_font; }
    [[nodiscard]] double sampleRate() const { return m_rate; }
    [[nodiscard]] bool velocityCutoff() const { return m_velocityCutoff; }

    // ---- Audio thread.
    // The controls as they are now: applied to the notes playing and to every new one.
    void apply(const std::array<double, kParamCount>& values);
    void process(std::span<const MidiEvent> events, AudioBlock io);
    void releaseAll();
    [[nodiscard]] bool sounding() const;

private:
    Synth() = default;
    void handle(const MidiEvent& e);
    void overrideVoices(bool newOnly);
    [[nodiscard]] bool drums() const;

    std::shared_ptr<Font> m_font;
    double m_rate = 48000.0;
    bool m_velocityCutoff = true;
    _fluid_hashtable_t* m_settings = nullptr;
    _fluid_synth_t* m_synth = nullptr;
    std::array<double, kParamCount> m_values{};
    std::vector<Override> m_overrides;      // (reserved: never grows on the audio thread)
    long long m_lastVoiceId = -1;            // the newest note's voices already set (FluidSynth counts from 0)
    int m_preset = -1;                       // the preset selected (index into the font's)
};

// What a node holds: the controls, the file, the synth playing it, and the
// hand-over of a new synth (main thread) to the audio thread.
class Player
{
public:
    enum class FileState : uint8_t { None, Loading, Ready, Failed };
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    // ---- Main thread.
    void prepare(double sampleRate);
    // Reads `path` (empty: none) and plays it once read; the preset `bank`:`program`
    // is chosen then (-1: the file's first). Problems come through takeMessages().
    void setFile(const QString& path, int bank = -1, int program = -1);
    [[nodiscard]] QString file() const { return m_path; }
    [[nodiscard]] FileState fileState() const;
    // Changes each time another file's presets are in (the window reloads its list).
    [[nodiscard]] int generation() const { return m_generation; }
    [[nodiscard]] std::vector<PresetInfo> presets() const;
    // The preset chosen, or {-1, -1}.
    [[nodiscard]] std::pair<int, int> chosen() const;
    // Builds, swaps and frees synths as needed (the engine's poll).
    void service();
    [[nodiscard]] QStringList takeMessages() { return std::exchange(m_messages, {}); }

    // ---- Any thread.
    void setParam(uint32_t id, double value);
    [[nodiscard]] double param(uint32_t id) const;
    void requestReleaseAll() { m_releaseAll.store(true, std::memory_order_relaxed); }

    // ---- Audio thread.
    void process(std::span<const MidiEvent> events, AudioBlock io);
    [[nodiscard]] bool holdsNotes() const noexcept { return m_holds.load(std::memory_order_relaxed); }

private:
    void choosePreset(int bank, int program); // main thread, once the font is in
    void say(const QString& message);         // main thread: logged, and kept for takeMessages()

    std::array<std::atomic<double>, kParamCount> m_params;
    // Synths are owned through these: built on the main thread, handed to the
    // audio thread (m_incoming), played (m_live), handed back (m_retired) and
    // freed on the main thread.
    std::atomic<Synth*> m_incoming{nullptr};
    std::atomic<Synth*> m_retired{nullptr};
    Synth* m_live = nullptr; // audio thread's
    std::atomic<bool> m_dropLive{false}; // main -> audio: no file any more, stop playing
    std::atomic<bool> m_releaseAll{false};
    std::atomic<bool> m_holds{false};

    // ---- Main thread.
    double m_rate = 0.0;
    QString m_path;
    std::shared_ptr<Font> m_font;     // being read, or read
    const Font* m_adopted = nullptr;  // the font whose presets the controls know
    std::pair<int, int> m_want{-1, -1};
    bool m_failedReported = false;
    int m_generation = 0;
    QStringList m_messages;
    // What the synth last built plays (another is built when this changes).
    const Font* m_builtFont = nullptr;
    double m_builtRate = 0.0;
    bool m_builtVelocityCutoff = true;
    bool m_buildFailed = false;
};

} // namespace gigchain::engine::sf2
