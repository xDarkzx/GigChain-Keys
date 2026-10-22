#include "PianoLibrary.h"

#include "EngineLog.h"

#include <QCoreApplication>

#include <cmath>

using namespace Qt::StringLiterals;

namespace gigchain::engine::piano {

namespace {

std::pair<int, long long> keyOf(Source which, double sampleRate)
{
    return {static_cast<int>(which), std::llround(sampleRate)};
}

} // namespace

QString sfzFileFor(Source which)
{
    return which == Source::Upright ? u"Upright Piano, Y.sfz"_s : u"Grand Piano, K.sfz"_s;
}

PianoLibrary& PianoLibrary::instance()
{
    static PianoLibrary library;
    return library;
}

PianoLibrary::PianoLibrary()
{
    // Reads stop before the app (and Qt Multimedia, which they decode with) goes.
    if (auto* app = QCoreApplication::instance(); app != nullptr) {
        QObject::connect(app, &QCoreApplication::aboutToQuit, &m_quitWatch, [this] { shutdown(); });
    }
}

PianoLibrary::~PianoLibrary()
{
    shutdown();
}

const std::atomic<const SampleSet*>* PianoLibrary::request(Source which, double sampleRate)
{
    if (!recorded(which) || !std::isfinite(sampleRate) || sampleRate <= 0.0) return nullptr;
    const std::scoped_lock lock(m_mutex);
    if (const auto found = m_entries.find(keyOf(which, sampleRate)); found != m_entries.end()) return &found->second->ready;
    if (m_shutDown) return nullptr; // the app is quitting: no new read
    auto& entry = m_entries[keyOf(which, sampleRate)];
    entry = std::make_unique<Entry>();
    Entry* e = entry.get();
    const QString path = pianoSampleFolder() + u'/' + sfzFileFor(which);
    e->loader = std::thread([e, path, sampleRate] {
        auto loaded = loadSampleSet(path, sampleRate, {}, &e->cancel);
        if (!loaded) {
            e->problem = loaded.error().message;
            qCWarning(lcEngine).noquote() << "Piano Pro plays its FM piano instead:" << e->problem;
            e->state.store(State::Failed, std::memory_order_release);
            return;
        }
        e->set = std::make_unique<SampleSet>(std::move(*loaded));
        e->ready.store(e->set.get(), std::memory_order_release);
        e->state.store(State::Ready, std::memory_order_release);
    });
    return &e->ready;
}

const PianoLibrary::Entry* PianoLibrary::find(Source which, double sampleRate) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_entries.find(keyOf(which, sampleRate));
    return found != m_entries.end() ? found->second.get() : nullptr;
}

PianoLibrary::State PianoLibrary::state(Source which, double sampleRate) const
{
    const Entry* e = find(which, sampleRate);
    return e != nullptr ? e->state.load(std::memory_order_acquire) : State::NotAsked;
}

QString PianoLibrary::problem(Source which, double sampleRate) const
{
    const Entry* e = find(which, sampleRate);
    return e != nullptr && e->state.load(std::memory_order_acquire) == State::Failed ? e->problem : QString();
}

void PianoLibrary::shutdown()
{
    const std::scoped_lock lock(m_mutex);
    m_shutDown = true;
    for (auto& [key, entry] : m_entries) entry->cancel.store(true, std::memory_order_relaxed);
    for (auto& [key, entry] : m_entries) {
        if (entry->loader.joinable()) entry->loader.join();
    }
}

} // namespace gigchain::engine::piano
