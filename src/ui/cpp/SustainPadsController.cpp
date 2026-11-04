#include "SustainPadsController.h"

#include "DocumentController.h"
#include "EffectWindows.h"
#include "InstrumentWindows.h"
#include "gigchain/engine/IEngine.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kPresetKey = u"sustainPads/preset"_s;
const QString kKeyKey = u"sustainPads/key"_s;
const QString kOctaveKey = u"sustainPads/octave"_s;
const QString kFadeKey = u"sustainPads/fadeSeconds"_s;
const QString kToneKey = u"sustainPads/tone"_s;
const QString kShimmerKey = u"sustainPads/shimmer"_s;
const QString kMotionKey = u"sustainPads/motion"_s;
const QString kVolumeKey = u"sustainPads/volumeDb"_s;
const QString kOutputKey = u"sustainPads/outputPair"_s;
const QString kFollowKey = u"sustainPads/followSongKey"_s;
const QString kShownKey = u"sustainPads/shown"_s;
const QString kDetachedKey = u"sustainPads/detached"_s;
const QString kGeometryKey = u"sustainPads/windowGeometry"_s;
const QString kMuteKey = u"sustainPads/mute"_s;
const QString kInstrumentKey = u"sustainPads/instrument"_s;         // its plugin id ("" or none: Pads Pro)
const QString kInstrumentNameKey = u"sustainPads/instrumentName"_s;
const QString kInstrumentStateKey = u"sustainPads/instrumentState"_s; // its settings (base64)

constexpr double kMinFade = 0.1;
constexpr double kMaxFade = 30.0;
constexpr double kMinVolume = -60.0;
constexpr double kMaxVolume = 0.0;

double unit(double v, double fallback) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : fallback; }

} // namespace

SustainPadsController::SustainPadsController(engine::IEngine& engine, DocumentController& document, QSettings& settings,
                                             EffectWindows& windows, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_store(settings), m_lastSong(document.songIndex()), m_windows(windows)
{
    m_settings.active = false; // never by itself when the app opens
    m_settings.preset = std::clamp(settings.value(kPresetKey, 1).toInt(), 0, static_cast<int>(engine::kSustainPadPresets.size()) - 1);
    m_settings.key = std::clamp(settings.value(kKeyKey, 0).toInt(), 0, 11);
    m_settings.octave = std::clamp(settings.value(kOctaveKey, 0).toInt(), -1, 1);
    m_settings.fadeSeconds = std::clamp(settings.value(kFadeKey, 4.0).toDouble(), kMinFade, kMaxFade);
    m_settings.tone = unit(settings.value(kToneKey, 0.5).toDouble(), 0.5);
    m_settings.shimmer = unit(settings.value(kShimmerKey, 0.3).toDouble(), 0.3);
    m_settings.motion = unit(settings.value(kMotionKey, 0.5).toDouble(), 0.5);
    m_settings.volumeDb = std::clamp(settings.value(kVolumeKey, -6.0).toDouble(), kMinVolume, kMaxVolume);
    m_settings.outputPair = std::max(settings.value(kOutputKey, 0).toInt(), 0);
    m_followSongKey = settings.value(kFollowKey, true).toBool();
    m_shown = settings.value(kShownKey, false).toBool();
    m_detached = settings.value(kDetachedKey, false).toBool();
    m_settings.mute = settings.value(kMuteKey, false).toBool();
    m_engine.setSustainPads(m_settings);

    // The VST it played last time, with the patch chosen then. Not there
    // now (uninstalled, a crash): Pads Pro plays, and it is said why; the
    // choice is kept for when it is back.
    if (const QString id = settings.value(kInstrumentKey).toString(); !id.isEmpty()) {
        const QString name = settings.value(kInstrumentNameKey, id).toString();
        const QByteArray state = QByteArray::fromBase64(settings.value(kInstrumentStateKey).toByteArray());
        if (auto loaded = m_engine.setSustainPadsInstrument(id, state); loaded) {
            m_instrumentId = id;
            m_instrumentName = name;
        } else {
            m_document.reportMessage(tr("Sustain Pads: %1 could not be loaded (%2). Pads Pro plays instead.").arg(name, loaded.error().message));
        }
    }
    m_keepSoon.setSingleShot(true);
    m_keepSoon.setInterval(1500);
    connect(&m_keepSoon, &QTimer::timeout, this, &SustainPadsController::keepInstrumentState);
    connect(&m_windows, &EffectWindows::busWindowClosed, this, [this](int bus) {
        if (bus == static_cast<int>(EffectWindows::Bus::SustainPads)) keepInstrumentState();
    });

    connect(&m_document, &DocumentController::currentChanged, this, &SustainPadsController::songPicked);
    connect(&m_document, &DocumentController::songChanged, this, &SustainPadsController::songPicked);
    connect(&m_document, &DocumentController::chartChanged, this, &SustainPadsController::songPicked);
    m_lastSongKey = songKey();
}

void SustainPadsController::apply()
{
    m_engine.setSustainPads(m_settings);
    m_store.setValue(kPresetKey, m_settings.preset);
    m_store.setValue(kKeyKey, m_settings.key);
    m_store.setValue(kOctaveKey, m_settings.octave);
    m_store.setValue(kFadeKey, m_settings.fadeSeconds);
    m_store.setValue(kToneKey, m_settings.tone);
    m_store.setValue(kShimmerKey, m_settings.shimmer);
    m_store.setValue(kMotionKey, m_settings.motion);
    m_store.setValue(kVolumeKey, m_settings.volumeDb);
    m_store.setValue(kOutputKey, m_settings.outputPair);
    m_store.setValue(kMuteKey, m_settings.mute);
    emit settingsChanged();
}

SustainPadsController::~SustainPadsController()
{
    keepInstrumentState(); // (the patch chosen in its window, for next time)
}

void SustainPadsController::setMuted(bool on)
{
    if (m_settings.mute == on) return;
    m_settings.mute = on;
    apply();
}

bool SustainPadsController::loadInstrument(const QString& pluginId, const QString& name)
{
    if (pluginId.isEmpty()) {
        usePadsPro();
        return true;
    }
    if (pluginId == m_instrumentId) return true;
    keepInstrumentState(); // (the one it replaces: its patch is kept no further, but nothing is lost before it goes)
    if (auto loaded = m_engine.setSustainPadsInstrument(pluginId, {}); !loaded) {
        m_document.reportMessage(tr("Sustain Pads: %1 could not be loaded: %2").arg(name, loaded.error().message));
        return false;
    }
    m_windows.closeSustainPads();
    m_instrumentId = pluginId;
    m_instrumentName = name.isEmpty() ? pluginId : name;
    m_store.setValue(kInstrumentKey, m_instrumentId);
    m_store.setValue(kInstrumentNameKey, m_instrumentName);
    m_store.remove(kInstrumentStateKey); // (a new one starts as it loads)
    qCInfo(lcUi).noquote() << "Sustain Pads play" << m_instrumentName;
    emit instrumentChanged();
    return true;
}

void SustainPadsController::usePadsPro()
{
    if (m_instrumentId.isEmpty()) return;
    m_windows.closeSustainPads();
    if (auto back = m_engine.setSustainPadsInstrument({}, {}); !back) {
        m_document.reportMessage(tr("Sustain Pads: %1").arg(back.error().message)); // (never expected: it only lets go)
        return;
    }
    m_instrumentId.clear();
    m_instrumentName.clear();
    m_store.remove(kInstrumentKey);
    m_store.remove(kInstrumentNameKey);
    m_store.remove(kInstrumentStateKey);
    qCInfo(lcUi) << "Sustain Pads play Pads Pro";
    emit instrumentChanged();
}

bool SustainPadsController::openInstrumentWindow(QWindow* owner)
{
    if (m_instrumentId.isEmpty()) return false; // (Pads Pro is set here, in the panel)
    return m_windows.openSustainPads(m_instrumentId, m_instrumentName, owner);
}

void SustainPadsController::keepInstrumentState()
{
    if (m_instrumentId.isEmpty()) return;
    const auto state = m_engine.sustainPadsInstrumentState();
    if (!state) {
        qCWarning(lcUi).noquote() << "Sustain Pads:" << m_instrumentName << "settings could not be kept:" << state.error().message;
        return;
    }
    m_store.setValue(kInstrumentStateKey, state->toBase64());
}

void SustainPadsController::setActive(bool on)
{
    if (m_settings.active == on) return;
    m_settings.active = on;
    const QString playing = m_instrumentId.isEmpty()
                                ? QString::fromLatin1(engine::kSustainPadPresets.at(static_cast<std::size_t>(m_settings.preset)))
                                : m_instrumentName;
    qCInfo(lcUi).noquote() << (on ? u"Sustain Pads on: %1 in %2"_s.arg(playing, keyNames().at(m_settings.key)) : u"Sustain Pads fading out"_s);
    apply();
}

void SustainPadsController::setKey(int to)
{
    const int k = ((to % 12) + 12) % 12;
    if (m_settings.key == k) return;
    m_settings.key = k;
    apply();
}

void SustainPadsController::setPreset(int to)
{
    const int p = std::clamp(to, 0, static_cast<int>(engine::kSustainPadPresets.size()) - 1);
    if (m_settings.preset == p) return;
    m_settings.preset = p;
    apply();
}

void SustainPadsController::setOctave(int to)
{
    const int o = std::clamp(to, -1, 1);
    if (m_settings.octave == o) return;
    m_settings.octave = o;
    apply();
}

void SustainPadsController::setFadeSeconds(double seconds)
{
    const double s = std::isfinite(seconds) ? std::clamp(seconds, kMinFade, kMaxFade) : m_settings.fadeSeconds;
    if (m_settings.fadeSeconds == s) return;
    m_settings.fadeSeconds = s;
    apply();
}

void SustainPadsController::setTone(double to)
{
    const double t = unit(to, m_settings.tone);
    if (m_settings.tone == t) return;
    m_settings.tone = t;
    apply();
}

void SustainPadsController::setShimmer(double to)
{
    const double s = unit(to, m_settings.shimmer);
    if (m_settings.shimmer == s) return;
    m_settings.shimmer = s;
    apply();
}

void SustainPadsController::setMotion(double to)
{
    const double m = unit(to, m_settings.motion);
    if (m_settings.motion == m) return;
    m_settings.motion = m;
    apply();
}

void SustainPadsController::setVolumeDb(double db)
{
    const double v = std::isfinite(db) ? std::clamp(db, kMinVolume, kMaxVolume) : m_settings.volumeDb;
    if (m_settings.volumeDb == v) return;
    m_settings.volumeDb = v;
    apply();
}

void SustainPadsController::setOutputPair(int pair)
{
    const int p = std::max(pair, 0);
    if (m_settings.outputPair == p) return;
    m_settings.outputPair = p;
    apply();
}

void SustainPadsController::setFollowSongKey(bool follow)
{
    if (m_followSongKey == follow) return;
    m_followSongKey = follow;
    m_store.setValue(kFollowKey, follow);
    emit settingsChanged();
    if (follow && songKeyIndex() >= 0) setKey(songKeyIndex());
}

QStringList SustainPadsController::keyNames()
{
    return {u"C"_s, u"Db"_s, u"D"_s, u"Eb"_s, u"E"_s, u"F"_s, u"F#"_s, u"G"_s, u"Ab"_s, u"A"_s, u"Bb"_s, u"B"_s};
}

QStringList SustainPadsController::presetNames()
{
    QStringList names;
    std::ranges::transform(engine::kSustainPadPresets, std::back_inserter(names), [](const char* n) { return QString::fromLatin1(n); });
    return names;
}

QString SustainPadsController::songKey() const
{
    const int song = m_document.songIndex();
    const auto& songs = m_document.setlist().songs;
    if (song < 0 || std::cmp_greater_equal(song, songs.size())) return {};
    return songs.at(static_cast<std::size_t>(song)).key.trimmed();
}

int SustainPadsController::keyOf(const QString& written)
{
    const QString k = written.trimmed();
    if (k.isEmpty()) return -1;
    static const QString kLetters = u"C D EF G A B"_s; // their semitones above C
    const auto letter = static_cast<int>(kLetters.indexOf(k.at(0).toUpper()));
    if (letter < 0) return -1;
    int key = letter;
    if (k.size() > 1) {
        const QChar accidental = k.at(1);
        if (accidental == u'#' || accidental == QChar(0x266F)) ++key;
        else if (accidental == u'b' || accidental == QChar(0x266D)) --key;
    }
    return ((key % 12) + 12) % 12;
}

void SustainPadsController::songPicked()
{
    const int song = m_document.songIndex();
    const QString written = songKey();
    const bool changed = song != m_lastSong || written != m_lastSongKey;
    m_lastSong = song;
    m_lastSongKey = written;
    if (!changed) return; // (its tempo or track: a key tapped on the panel stays)
    emit songKeyChanged();
    // A song picked, or its key set, with a key of its own: the pad goes there.
    if (m_followSongKey && songKeyIndex() >= 0) setKey(songKeyIndex());
}

void SustainPadsController::pickKey(int tapped)
{
    // Only the key: never starts or stops the pad (Activate does). Off, it
    // waits in that key; playing, it fades over to it.
    setKey(tapped);
}

void SustainPadsController::setShown(bool on)
{
    if (m_shown == on) return;
    m_shown = on;
    m_store.setValue(kShownKey, on);
    emit placeChanged();
}

void SustainPadsController::setDetached(bool on)
{
    if (m_detached == on) return;
    m_detached = on;
    m_store.setValue(kDetachedKey, on);
    qCInfo(lcUi) << (on ? "Sustain Pads detached into a window of its own" : "Sustain Pads reattached");
    emit placeChanged();
}

QRect SustainPadsController::restoredGeometry() const
{
    QList<QRect> screens;
    std::ranges::transform(QGuiApplication::screens(), std::back_inserter(screens),
                           [](const QScreen* screen) { return screen->availableGeometry(); });
    return InstrumentWindows::onScreens(m_store.value(kGeometryKey).toRect(), screens);
}

void SustainPadsController::rememberGeometry(const QRect& normal)
{
    if (normal.isValid() && !normal.isEmpty()) m_store.setValue(kGeometryKey, normal);
}

void SustainPadsController::poll()
{
    // Its VST's settings changed in its window (a new sound): kept soon,
    // once the changes settle (not on every knob step).
    if (m_engine.takeSustainPadsEdits()) m_keepSoon.start();
    const engine::LevelReading level = m_engine.sustainPadsLevel();
    if (level.peak == m_peak && level.rms == m_rms) return;
    m_peak = level.peak;
    m_rms = level.rms;
    emit levelChanged();
}

} // namespace gigchain::ui
