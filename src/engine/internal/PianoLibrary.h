#pragma once

#include "PianoSamples.h"
#include "PianoTypes.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace gigchain::engine::piano {

// The recorded pianos, each read once per playing rate on its own thread
// and shared by every Piano Pro (a grand is ~150 MB in memory: never twice).
// A piano stays in memory until the app quits (or shutdown()).
class PianoLibrary
{
public:
    enum class State : uint8_t { NotAsked, Loading, Ready, Failed };

    static PianoLibrary& instance();
    ~PianoLibrary();
    PianoLibrary(const PianoLibrary&) = delete;
    PianoLibrary& operator=(const PianoLibrary&) = delete;
    PianoLibrary(PianoLibrary&&) = delete;
    PianoLibrary& operator=(PianoLibrary&&) = delete;

    // Main thread. Where recorded piano `which` at `sampleRate` will be
    // (nullptr in it until it is read; the audio thread reads it each
    // block); reading starts the first time it is asked for. nullptr for
    // an FM source.
    const std::atomic<const SampleSet*>* request(Source which, double sampleRate);
    [[nodiscard]] State state(Source which, double sampleRate) const;
    // Why it could not be read (empty when it was, or is still being read).
    [[nodiscard]] QString problem(Source which, double sampleRate) const;
    // Stops every read under way and waits for it (the app quitting); no
    // read starts after it (request() answers nullptr: the FM piano plays).
    void shutdown();

private:
    PianoLibrary();
    struct Entry
    {
        std::atomic<const SampleSet*> ready{nullptr};
        std::atomic<State> state{State::Loading};
        std::atomic<bool> cancel{false};
        std::unique_ptr<SampleSet> set;
        QString problem; // written before `state` turns Failed
        std::thread loader;
    };
    [[nodiscard]] const Entry* find(Source which, double sampleRate) const;

    mutable std::mutex m_mutex;
    std::map<std::pair<int, long long>, std::unique_ptr<Entry>> m_entries;
    bool m_shutDown = false; // (under m_mutex)
    QObject m_quitWatch; // ties shutdown() to the app's aboutToQuit
};

// The SFZ map of recorded piano `which` (in pianoSampleFolder()).
[[nodiscard]] QString sfzFileFor(Source which);

} // namespace gigchain::engine::piano
