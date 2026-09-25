#include "SettingsController.h"

#include "DocumentController.h"

#include "gigchain/engine/IEngine.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QVariantMap>

#include <algorithm>
#include <iterator>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kDriverKey = u"audio/driver"_s;
const QString kDeviceKey = u"audio/device"_s;
const QString kRateKey = u"audio/sampleRate"_s;
const QString kBufferKey = u"audio/bufferFrames"_s;
const QString kMidiConfiguredKey = u"midi/configured"_s;
const QString kMidiEnabledKey = u"midi/enabled"_s;
const QString kMidiChannelsKey = u"midi/channels"_s;
const QString kControlsKey = u"midi/controls"_s; // one packed trigger per action
const QString kLimiterKey = u"master/limiter"_s;
const QString kLimiterCeilingKey = u"master/limiterCeilingDb"_s;
constexpr double kDefaultCeilingDb = -1.0;

engine::ControlTriggers savedControls(QSettings& settings)
{
    engine::ControlTriggers triggers{};
    const QVariantList saved = settings.value(kControlsKey).toList();
    for (qsizetype i = 0; i < saved.size() && i < engine::kControlActionCount; ++i) {
        triggers[static_cast<std::size_t>(i)] = engine::MidiTrigger::unpack(saved[i].toUInt());
    }
    return triggers;
}

double savedCeiling(QSettings& settings)
{
    bool ok = false;
    const double ceiling = settings.value(kLimiterCeilingKey, kDefaultCeilingDb).toDouble(&ok);
    return ok && std::isfinite(ceiling) ? std::clamp(ceiling, -24.0, 0.0) : kDefaultCeilingDb;
}

constexpr unsigned int kDefaultBuffer = 256;

QString driverName(engine::AudioDriver driver)
{
    return driver == engine::AudioDriver::Asio ? u"asio"_s : u"system"_s;
}

} // namespace

SettingsController::SettingsController(engine::IEngine& engine, DocumentController& document, QSettings& settings,
                                       QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_settings(settings)
{
    // Pedals and pads work from the first song.
    m_engine.setControlTriggers(savedControls(m_settings));
    // The limiter protects the sound desk from the first note.
    m_engine.setOutputLimiter(m_settings.value(kLimiterKey, true).toBool(), savedCeiling(m_settings));
}

engine::RealEngineOptions SettingsController::engineOptions(QSettings& settings)
{
    engine::RealEngineOptions options;
    options.audio.driver =
        settings.value(kDriverKey).toString() == u"asio"_s ? engine::AudioDriver::Asio : engine::AudioDriver::System;
    options.audio.device = settings.value(kDeviceKey).toString();
    options.audio.sampleRate = settings.value(kRateKey, 0).toUInt();
    options.audio.bufferFrames = settings.value(kBufferKey, kDefaultBuffer).toUInt();
    if (options.audio.bufferFrames == 0) options.audio.bufferFrames = kDefaultBuffer;
    options.midi.configured = settings.value(kMidiConfiguredKey, false).toBool();
    options.midi.enabled = settings.value(kMidiEnabledKey).toStringList();
    const QVariantMap channels = settings.value(kMidiChannelsKey).toMap();
    for (auto it = channels.begin(); it != channels.end(); ++it) options.midi.channels[it.key()] = it.value().toInt();
    return options;
}

void SettingsController::load()
{
    m_outputs = m_engine.audioOutputs();
    m_loaded = m_engine.audioSetup();
    m_pending = m_loaded;
    m_midi = m_engine.midiInputs();
    m_midiTouched = false;
    m_running = m_engine.statusText();
    m_error.clear();
    m_reopenLast = m_settings.value(DocumentController::reopenLastSetlistKey(), false).toBool();
    m_limiterOn = m_settings.value(kLimiterKey, true).toBool();
    m_limiterCeilingDb = savedCeiling(m_settings);
    m_controls = savedControls(m_settings);
    m_learning = -1;
    keepRateValid();
    emit changed();
}

void SettingsController::setLimiterEnabled(bool on)
{
    if (m_limiterOn == on) return;
    m_limiterOn = on;
    emit changed();
}

void SettingsController::setLimiterCeilingDb(double ceilingDb)
{
    if (!std::isfinite(ceilingDb)) return;
    const double clamped = std::clamp(ceilingDb, -24.0, 0.0);
    if (m_limiterCeilingDb == clamped) return;
    m_limiterCeilingDb = clamped;
    emit changed();
}

QVariantList SettingsController::controls() const
{
    static const char* const kLabels[] = {QT_TR_NOOP("Next song"), QT_TR_NOOP("Previous song"),
                                          QT_TR_NOOP("Next part"), QT_TR_NOOP("Previous part"),
                                          QT_TR_NOOP("Panic (stop all sound)")};
    static_assert(std::size(kLabels) == engine::kControlActionCount);
    QVariantList list;
    for (int i = 0; i < engine::kControlActionCount; ++i) {
        const engine::MidiTrigger& trigger = m_controls[static_cast<std::size_t>(i)];
        list << QVariantMap{{u"action"_s, i},
                            {u"label"_s, tr(kLabels[i])},
                            {u"trigger"_s, trigger.isSet() ? trigger.describe() : QString()}};
    }
    return list;
}

void SettingsController::learnControl(int action)
{
    if (action < 0 || action >= engine::kControlActionCount) {
        qCWarning(lcUi) << "Ignored: no control action" << action;
        return;
    }
    (void)m_engine.takeLearnedTrigger(); // only a press from now on counts
    m_learning = action;
    emit changed();
}

void SettingsController::clearControl(int action)
{
    if (action < 0 || action >= engine::kControlActionCount) {
        qCWarning(lcUi) << "Ignored: no control action" << action;
        return;
    }
    m_controls[static_cast<std::size_t>(action)] = {};
    if (m_learning == action) m_learning = -1;
    m_controlsTouched = true;
    emit changed();
}

void SettingsController::pollLearning()
{
    if (m_learning < 0) return;
    const engine::MidiTrigger pressed = m_engine.takeLearnedTrigger();
    if (!pressed.isSet()) return;
    // One control, one action: taken from any other action that had it.
    for (auto& trigger : m_controls) {
        if (trigger == pressed) trigger = {};
    }
    m_controls[static_cast<std::size_t>(m_learning)] = pressed;
    qCInfo(lcUi).noquote() << "Learned" << pressed.describe() << "for control" << m_learning;
    m_learning = -1;
    m_controlsTouched = true;
    emit changed();
}

QVariantList SettingsController::blockedPlugins() const
{
    QVariantList list;
    for (const QString& path : m_engine.blockedPlugins()) {
        list << QVariantMap{{u"path"_s, path}, {u"name"_s, QFileInfo(path).completeBaseName()}};
    }
    return list;
}

void SettingsController::unblockPlugin(const QString& path)
{
    m_engine.unblockPlugin(path);
    emit changed();
}

QVariantList SettingsController::limiterCeilings()
{
    return {-0.1, -0.3, -0.5, -1.0, -2.0, -3.0, -6.0};
}

void SettingsController::setReopenLastSetlist(bool reopen)
{
    if (m_reopenLast == reopen) return;
    m_reopenLast = reopen;
    emit changed();
}

QString SettingsController::driver() const
{
    return driverName(m_pending.driver);
}

void SettingsController::setDriver(const QString& driver)
{
    const auto wanted = driver == u"asio"_s ? engine::AudioDriver::Asio : engine::AudioDriver::System;
    if (wanted == m_pending.driver) return;
    m_pending.driver = wanted;
    const QStringList names = devices();
    m_pending.device = names.isEmpty() ? QString() : names.first();
    if (wanted == engine::AudioDriver::System) {
        for (const auto& output : m_outputs) {
            if (output.driver == wanted && output.isDefault) m_pending.device = output.name;
        }
    }
    keepRateValid();
    emit changed();
}

void SettingsController::setDevice(const QString& device)
{
    if (device == m_pending.device || !devices().contains(device)) return;
    m_pending.device = device;
    keepRateValid();
    emit changed();
}

QStringList SettingsController::devices() const
{
    QStringList names;
    for (const auto& output : m_outputs) {
        if (output.driver == m_pending.driver) names << output.name;
    }
    return names;
}

bool SettingsController::asioAvailable() const
{
    return std::any_of(m_outputs.begin(), m_outputs.end(),
                       [](const engine::AudioOutput& o) { return o.driver == engine::AudioDriver::Asio; });
}

const engine::AudioOutput* SettingsController::chosenOutput() const
{
    for (const auto& output : m_outputs) {
        if (output.driver == m_pending.driver && output.name == m_pending.device) return &output;
    }
    return nullptr;
}

void SettingsController::keepRateValid()
{
    const engine::AudioOutput* output = chosenOutput();
    if (output == nullptr || output->sampleRates.empty()) return;
    const auto& rates = output->sampleRates;
    if (std::find(rates.begin(), rates.end(), m_pending.sampleRate) != rates.end()) return;
    // The device's own rate if offered, else the nearest to 48 kHz.
    if (std::find(rates.begin(), rates.end(), output->preferredSampleRate) != rates.end()) {
        m_pending.sampleRate = output->preferredSampleRate;
        return;
    }
    m_pending.sampleRate = *std::min_element(rates.begin(), rates.end(), [](unsigned int a, unsigned int b) {
        return std::abs(static_cast<int>(a) - 48000) < std::abs(static_cast<int>(b) - 48000);
    });
}

void SettingsController::setSampleRate(int rate)
{
    if (rate <= 0 || static_cast<unsigned int>(rate) == m_pending.sampleRate) return;
    if (!sampleRates().contains(QVariant(rate))) return;
    m_pending.sampleRate = static_cast<unsigned int>(rate);
    emit changed();
}

QVariantList SettingsController::sampleRates() const
{
    QVariantList rates;
    if (const engine::AudioOutput* output = chosenOutput()) {
        for (const unsigned int rate : output->sampleRates) rates << static_cast<int>(rate);
    }
    return rates;
}

void SettingsController::setBufferFrames(int frames)
{
    if (frames <= 0 || static_cast<unsigned int>(frames) == m_pending.bufferFrames) return;
    if (!bufferSizes().contains(QVariant(frames))) return;
    m_pending.bufferFrames = static_cast<unsigned int>(frames);
    emit changed();
}

QVariantList SettingsController::bufferSizes()
{
    return {32, 64, 128, 256, 512, 1024, 2048};
}

double SettingsController::latencyMs() const
{
    return m_pending.sampleRate > 0 ? 1000.0 * m_pending.bufferFrames / m_pending.sampleRate : 0.0;
}

QVariantList SettingsController::midiInputs() const
{
    QVariantList list;
    for (const auto& port : m_midi) {
        list << QVariantMap{{u"name"_s, port.name}, {u"enabled"_s, port.enabled}, {u"channel"_s, port.channel}};
    }
    return list;
}

void SettingsController::setMidiInputEnabled(const QString& name, bool enabled)
{
    for (auto& port : m_midi) {
        if (port.name == name && port.enabled != enabled) {
            port.enabled = enabled;
            m_midiTouched = true;
            emit changed();
        }
    }
}

void SettingsController::setMidiInputChannel(const QString& name, int channel)
{
    if (channel < 0 || channel > 16) return;
    for (auto& port : m_midi) {
        if (port.name == name && port.channel != channel) {
            port.channel = channel;
            m_midiTouched = true;
            emit changed();
        }
    }
}

void SettingsController::refreshMidi()
{
    std::vector<engine::MidiPort> present = m_engine.midiInputs();
    for (auto& port : present) {
        // Keep what this page already shows (and maybe changed) for known inputs.
        const auto shown = std::find_if(m_midi.begin(), m_midi.end(),
                                        [&](const engine::MidiPort& p) { return p.name == port.name; });
        if (shown != m_midi.end()) port = *shown;
    }
    if (present == m_midi) return;
    m_midi = std::move(present);
    emit changed();
}

engine::MidiSetup SettingsController::pendingMidi() const
{
    engine::MidiSetup setup = m_engine.midiSetup();
    setup.configured = true;
    for (const auto& port : m_midi) {
        // Inputs not plugged in now keep their saved choice.
        setup.enabled.removeAll(port.name);
        if (port.enabled) setup.enabled << port.name;
        if (port.channel != 0) setup.channels[port.name] = port.channel;
        else setup.channels.erase(port.name);
    }
    return setup;
}

void SettingsController::resetToDefaults()
{
    m_pending.driver = engine::AudioDriver::System;
    m_pending.device.clear();
    for (const auto& output : m_outputs) {
        if (output.driver == engine::AudioDriver::System && output.isDefault) m_pending.device = output.name;
    }
    m_pending.sampleRate = 0;
    m_pending.bufferFrames = kDefaultBuffer;
    keepRateValid();
    for (std::size_t i = 0; i < m_midi.size(); ++i) {
        m_midi[i].enabled = i == 0; // the default: only the first port
        m_midi[i].channel = 0;
    }
    m_midiTouched = true;
    m_reopenLast = false;
    m_limiterOn = true;
    m_limiterCeilingDb = kDefaultCeilingDb;
    m_controls = {};
    m_learning = -1;
    m_controlsTouched = true;
    emit changed();
}

bool SettingsController::apply()
{
    m_error.clear();
    QStringList problems;

    if (m_pending != m_loaded) {
        if (auto changedAudio = m_engine.setAudioSetup(m_pending); !changedAudio) {
            problems << changedAudio.error().message; // logged by the engine
        } else {
            m_loaded = m_engine.audioSetup();
            m_settings.setValue(kDriverKey, driverName(m_pending.driver));
            m_settings.setValue(kDeviceKey, m_pending.device);
            m_settings.setValue(kRateKey, m_pending.sampleRate);
            m_settings.setValue(kBufferKey, m_pending.bufferFrames);
        }
    }

    if (m_midiTouched) {
        const engine::MidiSetup midi = pendingMidi();
        if (auto changedMidi = m_engine.setMidiSetup(midi); !changedMidi) {
            problems << changedMidi.error().message; // logged by the engine
        }
        // The choice itself stands even if an input failed to open (it is named above).
        QVariantMap channels;
        for (const auto& [name, channel] : midi.channels) channels.insert(name, channel);
        m_settings.setValue(kMidiConfiguredKey, true);
        m_settings.setValue(kMidiEnabledKey, midi.enabled);
        m_settings.setValue(kMidiChannelsKey, channels);
        m_midiTouched = false;
    }

    m_settings.setValue(DocumentController::reopenLastSetlistKey(), m_reopenLast);
    m_engine.setOutputLimiter(m_limiterOn, m_limiterCeilingDb);
    if (m_controlsTouched) {
        m_engine.setControlTriggers(m_controls);
        QVariantList packed;
        for (const auto& trigger : m_controls) packed << trigger.pack();
        m_settings.setValue(kControlsKey, packed);
        m_controlsTouched = false;
    }
    m_learning = -1;
    m_settings.setValue(kLimiterKey, m_limiterOn);
    m_settings.setValue(kLimiterCeilingKey, m_limiterCeilingDb);

    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        problems << tr("Settings could not be saved (%1)").arg(m_settings.fileName());
        qCWarning(lcUi).noquote() << problems.back();
    }

    m_running = m_engine.statusText();
    if (!problems.isEmpty()) {
        m_error = problems.join(u"\n"_s);
        m_document.reportMessage(m_error);
    }
    emit changed();
    return problems.isEmpty();
}

} // namespace gigchain::ui
