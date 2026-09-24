#include "SettingsController.h"

#include "DocumentController.h"

#include "openstage/engine/IEngine.h"

#include <QLoggingCategory>
#include <QSettings>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace openstage::ui {
namespace {

const QString kDriverKey = u"audio/driver"_s;
const QString kDeviceKey = u"audio/device"_s;
const QString kRateKey = u"audio/sampleRate"_s;
const QString kBufferKey = u"audio/bufferFrames"_s;
const QString kMidiOffKey = u"midi/inputsOff"_s;

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
    options.midiInputsOff = settings.value(kMidiOffKey).toStringList();
    return options;
}

void SettingsController::load()
{
    m_outputs = m_engine.audioOutputs();
    m_loaded = m_engine.audioSetup();
    m_pending = m_loaded;
    m_midi = m_engine.midiInputs();
    m_running = m_engine.statusText();
    m_error.clear();
    keepRateValid();
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
    for (const auto& port : m_midi) list << QVariantMap{{u"name"_s, port.name}, {u"enabled"_s, port.enabled}};
    return list;
}

void SettingsController::setMidiInputEnabled(const QString& name, bool enabled)
{
    for (auto& port : m_midi) {
        if (port.name == name && port.enabled != enabled) {
            port.enabled = enabled;
            emit changed();
        }
    }
}

QStringList SettingsController::midiOff() const
{
    QStringList off;
    for (const auto& port : m_midi) {
        if (!port.enabled) off << port.name;
    }
    return off;
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
    for (auto& port : m_midi) port.enabled = true;
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

    const QStringList off = midiOff();
    std::vector<engine::MidiPort> running = m_engine.midiInputs();
    const bool midiChanged = std::any_of(running.begin(), running.end(), [&](const engine::MidiPort& port) {
        return port.enabled == off.contains(port.name);
    });
    if (midiChanged) {
        if (auto changedMidi = m_engine.setMidiInputsOff(off); !changedMidi) {
            problems << changedMidi.error().message; // logged by the engine
        }
        m_settings.setValue(kMidiOffKey, off); // the switch itself took effect either way
    }

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

} // namespace openstage::ui
