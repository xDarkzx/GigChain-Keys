// The soak: a long gig on the real engine, run by tools\soak.ps1. Patches
// change every moment with chords held across them, loops are recorded,
// layered, played and cleared, a song's sections follow the chords played
// (switching instruments, handing the chord over) and jump, a backing
// track starts and stops, the tempo moves, and now and then the audio
// device is reopened (another buffer size) and MIDI set up again, as
// Settings does. Silent: the master is at -inf.
//
// Every 10 s it writes a row to the CSV (memory, handles, threads, CPU
// load, warnings). It fails when, after the warm-up, memory, handles or
// threads keep growing, when the audio drops out, or when anything is
// logged as a warning or error it does not expect.
//
//   <soak>.exe <minutes> <csv file>
#include "Handles.h"

#include "gigchain/core/Chords.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/Model.h"
#include "gigchain/engine/RealEngineFactory.h"
#include "gigchain/platform/MemoryUse.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// ------------------------------------------------------------- what is logged
struct Logged
{
    QMutex mutex;
    int warnings = 0;             // unexpected warnings and worse
    int dropouts = 0;             // "Audio dropped out"
    std::map<QString, int> kinds; // the first words of each unexpected one
};
Logged& logged()
{
    static Logged instance;
    return instance;
}

// Warnings the soak causes on purpose, or that plugins say while loading.
bool expected(const QString& text)
{
    static const QRegularExpression pattern(
        uR"(MidiIn(WinMM|Dummy)|no MIDI input devices|MIDI input .* reported|Skipping plugin|maps \d+ parameters|^Panic: every sound stopped|)"
        uR"(Listing MIDI (inputs|outputs) failed: Midi(In|Out)Alsa::initialize)"_s); // (WSL has no ALSA MIDI: said once)
    return pattern.match(text).hasMatch();
}

// The soak's report, on stderr (tools\soak.ps1 reads it). Any thread.
void say(const QString& line)
{
    static QMutex mutex;
    static QTextStream err(stderr);
    const QMutexLocker lock(&mutex);
    err << line << Qt::endl;
}

void handler(QtMsgType type, const QMessageLogContext&, const QString& text)
{
    if (type == QtDebugMsg || type == QtInfoMsg) return;
    {
        Logged& log = logged();
        const QMutexLocker lock(&log.mutex);
        if (text.contains(u"Audio dropped out"_s)) {
            ++log.dropouts;
        } else if (!expected(text)) {
            ++log.warnings;
            ++log.kinds[text.left(80)];
        }
    }
    say(text);
}

// ------------------------------------------------------------- measuring
struct Sample
{
    double minutes = 0.0;
    double privateMb = 0.0; // Windows: private bytes; Linux: resident memory
    unsigned long handles = 0; // Windows: kernel handles; Linux: open file descriptors
    int threads = 0;
    float cpu = 0.0F;
};

#ifndef _WIN32
Sample measure(double minutes, float cpu)
{
    const auto bytes = platform::residentBytes();
    std::map<QString, int> counts = test::handlesByType();
    const int threads = counts[u"Thread"_s];
    counts.erase(u"Thread"_s);
    unsigned long handles = 0;
    for (const auto& [kind, count] : counts) handles += static_cast<unsigned long>(count);
    return Sample{.minutes = minutes,
                  .privateMb = bytes ? static_cast<double>(*bytes) / (1024.0 * 1024.0) : 0.0,
                  .handles = handles,
                  .threads = threads,
                  .cpu = cpu};
}
#else
int threadCount()
{
    const DWORD self = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == self) ++count;
    }
    CloseHandle(snapshot);
    return count;
}

Sample measure(double minutes, float cpu)
{
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): the API's own extended struct
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    return Sample{.minutes = minutes,
                  .privateMb = static_cast<double>(memory.PrivateUsage) / (1024.0 * 1024.0),
                  .handles = handles,
                  .threads = threadCount(),
                  .cpu = cpu};
}
#endif

// ------------------------------------------------------------- the gig
// A mono 48 kHz WAV of a quiet tone: the backing track.
bool writeBackingTrack(const QString& path)
{
    constexpr quint32 kFrames = 48000 * 3;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32{36 + (kFrames * 2)};
    out.writeRawData("WAVEfmt ", 8);
    out << quint32{16} << quint16{1} << quint16{1} << quint32{48000} << quint32{96000} << quint16{2} << quint16{16};
    out.writeRawData("data", 4);
    out << quint32{kFrames * 2};
    for (quint32 i = 0; i < kFrames; ++i) out << static_cast<qint16>((i % 100) * 20);
    return out.status() == QDataStream::Ok;
}

// The plugins played: the ones the tests use when installed, else the first
// installed instrument and effect.
std::pair<QString, QString> choosePlugins(const std::vector<PluginInfo>& plugins)
{
    QString instrument;
    QString effect;
    for (const PluginInfo& p : plugins) {
        if (p.name == u"Piano V2"_s) instrument = p.id;
        if (p.name.contains(u"Kotelnikov"_s)) effect = p.id;
    }
    for (const PluginInfo& p : plugins) {
        if (instrument.isEmpty() && p.kind == PluginKind::Instrument) instrument = p.id;
        if (effect.isEmpty() && p.kind == PluginKind::Effect) effect = p.id;
    }
    return {instrument, effect};
}

core::PluginSlot slot(const QString& id)
{
    return core::PluginSlot{.pluginId = id, .displayName = id.section(u'/', -1), .bypass = false, .state = {}};
}

std::vector<core::Patch> makePatches(const QString& instrument, const QString& effect)
{
    std::vector<core::Patch> patches;
    // One instrument with an effect.
    core::Patch one = core::makePatch(u"One"_s);
    core::Channel piano = core::makeChannel(u"Piano"_s);
    if (!instrument.isEmpty()) piano.instrument = slot(instrument);
    if (!effect.isEmpty()) piano.effects.push_back(slot(effect));
    one.channels.push_back(piano);
    patches.push_back(one);
    // A split: two layers of it.
    core::Patch split = core::makePatch(u"Split"_s);
    core::Channel low = piano;
    low.id = core::ChannelId::generate();
    low.keyHigh = 59;
    core::Channel high = piano;
    high.id = core::ChannelId::generate();
    high.keyLow = 60;
    high.transpose = 12;
    split.channels = {low, high};
    patches.push_back(split);
    // Nothing: a break between songs.
    patches.push_back(core::makePatch(u"Empty"_s));
    return patches;
}

int soak()
{
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 3) {
        say(u"usage: <soak> <minutes> <csv file>"_s);
        return 2;
    }
    const double minutes = args.at(1).toDouble();
    QFile csvFile(args.at(2));
    if (minutes <= 0.0 || !csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        say(u"the minutes must be above 0 and the CSV file writable"_s);
        return 2;
    }
    QTextStream csv(&csvFile);
    csv << "minutes,private_mb,handles,threads,cpu_load,warnings,dropouts\n";

    QTemporaryDir dir;
    const QString track = dir.filePath(u"backing.wav"_s);
    if (!dir.isValid() || !writeBackingTrack(track)) {
        say(u"could not write the backing track"_s);
        return 2;
    }

    RealEngineOptions options;
    options.midiInputs = true; // opened and reopened like the app does (no notes expected)
    options.pluginCacheFile = dir.filePath(u"plugin-cache.json"_s);
    auto created = createRealEngine(options);
    if (!created) {
        say(u"no engine: "_s + created.error().message);
        return 2;
    }
    IEngine& engine = **created;
    engine.setMasterVolume(core::limits::kMinVolumeDb); // silent
    const auto [instrument, effect] = choosePlugins(engine.availablePlugins());
    say(u"playing %1 with %2"_s.arg(instrument, effect));
    const std::vector<core::Patch> patches = makePatches(instrument, effect);
    const core::SongId song = core::SongId::generate();
    engine.applyPatch(song, patches.front());
    engine.setBackingTrack(track);
    // The split's song: its low layer plays the verse, its high one the
    // chorus, and the chords played lead (Am F | C G): the chart follows,
    // sections switch on the note and the held chord is handed over.
    const core::Patch& split = patches.at(1);
    engine.setSongSections(SongSections{.patch = split.id,
                                        .sections = {{.bars = 4, .live = {split.channels.at(0).id}},
                                                     {.bars = 4, .live = {split.channels.at(1).id}}},
                                        .switchEarly = false});
    ChordFollowMap follow;
    for (const auto& [name, section] : {std::pair{"Am", 0}, {"F", 0}, {"C", 1}, {"G", 1}}) {
        follow.steps.push_back(followStepOf(*core::parseChordName(QString::fromLatin1(name)), section));
    }
    follow.sectionStarts = {0, 2};
    if (auto set = engine.setChordFollow(follow); !set) {
        say(u"chord follow refused: "_s + set.error().message);
        return 2;
    }
    // Each step plays the song's next chord (on MIDI channel 1).
    const std::array<std::array<int, 3>, 4> chords{{{57, 60, 64}, {53, 57, 60}, {48, 52, 55}, {55, 59, 62}}};
    bool heard = false;    // an instrument sounded (the notes reached it)
    bool followed = false; // the chart moved with the chords

    std::vector<Sample> samples;
    std::map<QString, int> warmTypes;
    QElapsedTimer clock;
    clock.start();
    const auto elapsedMinutes = [&clock] { return static_cast<double>(clock.elapsed()) / 60000.0; };
    qint64 nextSample = 0;
    int step = 0;
    const AudioSetup original = engine.audioSetup();
    while (elapsedMinutes() < minutes) {
        const core::Patch& patch = patches.at(static_cast<std::size_t>(step) % patches.size());
        engine.applyPatch(song, patch);
        // A chord held across the change, let go after it.
        const std::array<int, 3>& chord = chords.at(static_cast<std::size_t>(step) % chords.size());
        for (const int note : chord) engine.injectNote(1, note, 90);
        if (!patch.channels.empty()) {
            const core::ChannelId& channel = patch.channels.front().id;
            switch (step % 6) {
            case 0: // record
            case 1: // close it
            case 2: // a layer on top
                engine.loopCommand(channel, LoopCommand::Record);
                break;
            case 3: engine.loopCommand(channel, LoopCommand::Undo); break;
            case 4: engine.loopCommand(channel, LoopCommand::PlayStop); break;
            default: engine.clearAllLoops(); break;
            }
        }
        if (step % 4 == 0) engine.playBackingTrack(true);
        if (step % 4 == 2) engine.playBackingTrack(false);
        if (step % 5 == 0) engine.setTempo(80.0 + (step % 7) * 20.0);
        if (step % 50 == 25) engine.panic();
        if (step % 30 == 10) engine.jumpToSection(step % 2); // the pedal's "next section"
        // Settings: another buffer size (the device reopens), and back.
        if (step % 200 == 100) {
            AudioSetup other = original;
            other.bufferFrames = original.bufferFrames == 256 ? 128 : 256;
            if (auto changed = engine.setAudioSetup(other); !changed) qWarning("%s", qPrintable(changed.error().message));
        }
        if (step % 200 == 150) {
            if (auto changed = engine.setAudioSetup(original); !changed) qWarning("%s", qPrintable(changed.error().message));
            if (auto midi = engine.setMidiSetup(engine.midiSetup()); !midi) qWarning("%s", qPrintable(midi.error().message));
        }
        // The app's poll, several times while the chord rings.
        for (int i = 0; i < 10; ++i) {
            for (const Notice& notice : engine.poll()) {
                if (notice.level != Notice::Level::Info) qWarning("notice: %s", qPrintable(notice.text));
            }
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        if (!patch.channels.empty()) heard = heard || engine.channelLevel(patch.channels.front().id).peak > 0.0F;
        followed = followed || engine.chordFollow().step > 0;
        for (const int note : chord) engine.injectNote(1, note, 0);
        ++step;

        if (clock.elapsed() >= nextSample) {
            nextSample += 10'000;
            const Sample s = measure(elapsedMinutes(), engine.cpuLoad());
            samples.push_back(s);
            if (samples.size() == 3) warmTypes = test::handlesByType(); // after the plugins loaded
            Logged& log = logged();
            const QMutexLocker lock(&log.mutex);
            csv << QString::number(s.minutes, 'f', 2) << ',' << QString::number(s.privateMb, 'f', 1) << ',' << s.handles << ','
                << s.threads << ',' << QString::number(s.cpu, 'f', 3) << ',' << log.warnings << ',' << log.dropouts << '\n';
            csv.flush();
        }
    }

    // ------------------------------------------------------------- verdict
    // Which kinds of handle grew since the warm-up (names the leak, if any).
    for (const auto& [type, count] : test::handlesByType()) {
        const auto warm = warmTypes.find(type);
        const int before = warm == warmTypes.end() ? 0 : warm->second;
        if (count != before) say(u"handles: %1 %2 -> %3"_s.arg(type).arg(before).arg(count));
    }
    QStringList problems;
    // What was soaked has to have run: notes that never reach an instrument
    // prove nothing (a soak once played on MIDI channel 0, which is none).
    if (!instrument.isEmpty() && !heard) problems << u"no note was heard: the chords never reached the instrument"_s;
    if (!followed) problems << u"the chart never followed the chords played"_s;
    if (samples.size() < 6) problems << u"too short to judge (at least a minute)"_s;
    else {
        // After the warm-up (the first fifth: plugins load, caches fill), the
        // last readings must not be above the early ones by more than noise.
        const std::size_t warm = samples.size() / 5;
        const auto early = [&](auto field) {
            double sum = 0.0;
            for (std::size_t i = warm; i < warm + 3; ++i) sum += field(samples.at(i));
            return sum / 3.0;
        };
        const auto late = [&](auto field) {
            double sum = 0.0;
            for (std::size_t i = samples.size() - 3; i < samples.size(); ++i) sum += field(samples.at(i));
            return sum / 3.0;
        };
        const double memoryGrowth = late([](const Sample& s) { return s.privateMb; }) - early([](const Sample& s) { return s.privateMb; });
        const auto handles = [](const Sample& s) { return static_cast<double>(s.handles); };
        const auto threads = [](const Sample& s) { return static_cast<double>(s.threads); };
        const double handleGrowth = late(handles) - early(handles);
        const double threadGrowth = late(threads) - early(threads);
        say(u"after warm-up: memory %1 MB, handles %2, threads %3 over %4 steps"_s.arg(memoryGrowth, 0, 'f', 1)
                .arg(handleGrowth, 0, 'f', 0)
                .arg(threadGrowth, 0, 'f', 0)
                .arg(step));
        if (memoryGrowth > 20.0) problems << u"memory grew by %1 MB"_s.arg(memoryGrowth, 0, 'f', 1);
        if (handleGrowth > 50.0) problems << u"handles grew by %1"_s.arg(handleGrowth, 0, 'f', 0);
        if (threadGrowth > 2.0) problems << u"threads grew by %1"_s.arg(threadGrowth, 0, 'f', 0);
    }
    {
        Logged& log = logged();
        const QMutexLocker lock(&log.mutex);
        if (log.dropouts > 0) problems << u"the audio dropped out %1 time(s)"_s.arg(log.dropouts);
        if (log.warnings > 0) {
            problems << u"%1 unexpected warning(s)"_s.arg(log.warnings);
            for (const auto& [kind, count] : log.kinds) problems << u"  %1 x %2"_s.arg(count).arg(kind);
        }
    }
    if (problems.isEmpty()) {
        say(u"SOAK PASSED: %1 steps in %2 minutes"_s.arg(step).arg(elapsedMinutes(), 0, 'f', 1));
        return 0;
    }
    say(u"SOAK FAILED:\n  "_s + problems.join(u"\n  "_s));
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    // Anything thrown ends the soak as a failure, saying what it was.
    try {
        const QCoreApplication app(argc, argv);
        qInstallMessageHandler(&handler);
        return soak();
    } catch (const std::exception& e) {
        say(u"SOAK FAILED: stopped by an error: "_s + QString::fromUtf8(e.what()));
    } catch (...) {
        say(u"SOAK FAILED: stopped by an error of an unknown kind"_s);
    }
    return 1;
}
