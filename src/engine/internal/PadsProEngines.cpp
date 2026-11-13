#include "PadsProEngines.h"

#include "EngineLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <numeric>

using namespace Qt::StringLiterals;

namespace gigchain::engine::pads {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTau = 2.0 * std::numbers::pi;

// A sine, read from a table (cycles in, any value).
const std::array<float, 4097>& sineTable()
{
    static const std::array<float, 4097> table = [] {
        std::array<float, 4097> t{};
        for (std::size_t i = 0; i < t.size(); ++i) t.at(i) = static_cast<float>(std::sin(kTau * static_cast<double>(i) / 4096.0));
        return t;
    }();
    return table;
}
double sine(double cycles)
{
    const std::array<float, 4097>& t = sineTable();
    const double p = cycles - std::floor(cycles);
    const double x = p * 4096.0;
    const auto i = std::min(static_cast<std::size_t>(x), std::size_t{4095});
    const double f = x - static_cast<double>(i);
    return t.at(i) + (t.at(i + 1) - t.at(i)) * f;
}

double wrap(double p)
{
    return p - std::floor(p);
}

// PolyBLEP (Välimäki & Huovilainen): the step's corner taken off.
double blep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}
double saw(double p, double dt)
{
    return 2.0 * p - 1.0 - blep(p, dt);
}
double pulse(double p, double width, double dt)
{
    return (p < width ? 1.0 : -1.0) + blep(p, dt) - blep(wrap(p - width + 1.0), dt);
}
double triangle(double p)
{
    return 4.0 * std::abs(p - 0.5) - 1.0;
}

double cents(double c)
{
    return std::exp2(c / 1200.0);
}

double smooth(double x)
{
    const double c = std::clamp(x, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

// A spectral loop read between samples (Catmull-Rom, wrapping round).
double loopRead(const std::vector<float>& loop, double at)
{
    constexpr std::size_t kMask = SpectralTables::kLength - 1;
    const auto whole = static_cast<std::size_t>(at);
    const double t = at - static_cast<double>(whole);
    const double y0 = loop.at((whole - 1) & kMask);
    const double y1 = loop.at(whole & kMask);
    const double y2 = loop.at((whole + 1) & kMask);
    const double y3 = loop.at((whole + 2) & kMask);
    return y1 + 0.5 * t * (y2 - y0 + t * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + t * (3.0 * (y1 - y2) + y3 - y0)));
}
double advance(double at, double step)
{
    at += step;
    constexpr auto kLength = static_cast<double>(SpectralTables::kLength);
    return at >= kLength ? at - kLength : at;
}

// A single-cycle wave read between samples.
double waveRead(const std::vector<float>& wave, double phase)
{
    const double x = wrap(phase) * kWaveLength;
    const auto i = static_cast<std::size_t>(x);
    const double f = x - static_cast<double>(i);
    const float a = wave.at(i % kWaveLength);
    const float b = wave.at((i + 1) % kWaveLength);
    return a + (b - a) * f;
}

// A one-pole's pole designed at 44.1 kHz (Cook's), at `rate`.
double poleAt(double pole, double rate)
{
    return std::pow(pole, 44100.0 / rate);
}

// The bow and the string (Smith's bow table: friction falls as the bow slips).
double bowTable(double v, double slope)
{
    const double s = std::abs(v * slope) + 0.75;
    return std::min(std::pow(s, -4.0), 1.0);
}
// The flute's jet (Cook's: a cubic, clamped).
double jetTable(double x)
{
    return std::clamp(x * (x * x - 1.0), -1.0, 1.0);
}

double bandPass(Svf& f, double x)
{
    float low = 0.0F;
    float high = 0.0F;
    f.tick(static_cast<float>(x), low, high);
    return (static_cast<double>(x) - low - high) / f.k;
}

// A vector wave's harmonics.
double vectorHarmonic(VectorWave w, int n)
{
    switch (w) {
    case VectorWave::Sine: return n == 1 ? 1.0 : (n == 2 ? 0.12 : 0.0);
    case VectorWave::Vox: {
        const double formant = 0.15 + std::exp(-std::pow((n - 3.5) / 1.2, 2.0)) + 0.6 * std::exp(-std::pow((n - 9.0) / 2.0, 2.0));
        return formant / std::pow(n, 0.6);
    }
    case VectorWave::Bright: return 1.0 / std::pow(n, 0.75);
    case VectorWave::Hollow: return n % 2 == 1 ? 1.0 / n : 0.0;
    case VectorWave::Glass:
        switch (n) {
        case 1: return 1.0;
        case 2: return 0.45;
        case 4: return 0.3;
        case 7: return 0.22;
        case 11: return 0.15;
        case 16: return 0.08;
        default: return 0.0;
        }
    case VectorWave::Reed: return (n % 2 == 1 ? 1.0 : 0.2) / n;
    case VectorWave::Breath: return 1.0 / std::pow(n, 1.6);
    }
    return 0.0;
}

// A wave's shape over its time (0-1): rolling in, breaking at 0.4, running back.
double waveSwell(double pos)
{
    return pos < 0.4 ? smooth(pos / 0.4) : std::exp(-(pos - 0.4) * 3.5);
}
double waveCrash(double pos)
{
    return pos < 0.4 ? 0.0 : std::exp(-(pos - 0.4) * 9.0);
}

constexpr std::array<double, 9> kFootage{0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0};
constexpr std::array<double, 5> kRankRatio{0.5, 1.0, 2.0, 3.0, 4.0};

} // namespace

namespace {

// A 16-bit PCM WAV (mono or stereo) into `zone` (the files tools/make-pad-samples.py writes).
QString readWav(const QString& path, SampleZone& zone)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return u"cannot open %1: %2"_s.arg(path, file.errorString());
    const QByteArray data = file.readAll();
    const auto u16 = [&data](qsizetype at) { return static_cast<uint32_t>(static_cast<uint8_t>(data.at(at))) | (static_cast<uint32_t>(static_cast<uint8_t>(data.at(at + 1))) << 8U); };
    const auto u32 = [&u16](qsizetype at) { return u16(at) | (u16(at + 2) << 16U); };
    if (data.size() < 12 || data.left(4) != "RIFF" || data.mid(8, 4) != "WAVE") return u"%1 is not a WAV file"_s.arg(path);
    int channels = 0;
    int bits = 0;
    qsizetype at = 12;
    while (at + 8 <= data.size()) {
        const QByteArray id = data.mid(at, 4);
        const auto size = static_cast<qsizetype>(u32(at + 4));
        const qsizetype body = at + 8;
        if (body + size > data.size()) return u"%1 is cut short"_s.arg(path);
        if (id == "fmt ") {
            if (u16(body) != 1) return u"%1 is not plain PCM"_s.arg(path);
            channels = static_cast<int>(u16(body + 2));
            zone.rate = static_cast<double>(u32(body + 4));
            bits = static_cast<int>(u16(body + 14));
        } else if (id == "data") {
            if (bits != 16 || (channels != 1 && channels != 2)) return u"%1: only 16-bit mono or stereo is read"_s.arg(path);
            const auto frames = static_cast<std::size_t>(size / (qsizetype{2} * channels));
            zone.left.resize(frames);
            zone.right.resize(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                const auto sample = [&](int ch) {
                    return static_cast<float>(static_cast<int16_t>(u16(body + static_cast<qsizetype>((i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)) * 2)))) / 32768.0F;
                };
                zone.left.at(i) = sample(0);
                zone.right.at(i) = channels == 2 ? sample(1) : zone.left.at(i);
            }
            return {};
        }
        at = body + size + (size & 1);
    }
    return u"%1 has no sound in it"_s.arg(path);
}

} // namespace

QString padSampleFolder()
{
    QString chosen = qEnvironmentVariable("GIGCHAIN_PAD_SAMPLES");
    if (!chosen.isEmpty()) return chosen;
    // The sound pack's (cmake/PianoSamples.cmake): a Mac app keeps it in its Resources; otherwise beside the app.
    QString resources = QCoreApplication::applicationDirPath() + u"/../Resources/samples/pads"_s;
    if (QFileInfo::exists(resources)) return resources;
    return QDir(QCoreApplication::applicationDirPath()).filePath(u"samples/pads"_s);
}

std::size_t SampleSet::pick(int note) const
{
    std::size_t best = 0;
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (std::abs(zones.at(i).root - note) < std::abs(zones.at(best).root - note)) best = i;
    }
    return best;
}

std::shared_ptr<const SampleSet> loadSampleSet(const QString& name)
{
    static std::mutex lock;
    static std::map<QString, std::shared_ptr<const SampleSet>> loaded;
    const std::scoped_lock guard(lock);
    if (const auto found = loaded.find(name); found != loaded.end()) return found->second;
    const QDir folder(padSampleFolder());
    QFile list(folder.filePath(name + u".txt"_s));
    std::shared_ptr<SampleSet> set;
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcEngine).noquote() << "Pads Pro: no recordings for" << name << "at" << list.fileName() << "(" << list.errorString()
                                      << ") - its pads play their synth instead";
    } else {
        set = std::make_shared<SampleSet>();
        const QStringList lines = QString::fromUtf8(list.readAll()).split(u'\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            const QStringList parts = line.trimmed().split(u' ', Qt::SkipEmptyParts);
            if (parts.size() != 4) continue;
            SampleZone zone;
            zone.root = parts.at(1).toInt();
            zone.loopStart = parts.at(2).toULongLong();
            zone.loopEnd = parts.at(3).toULongLong();
            const QString why = readWav(folder.filePath(name + u"/"_s + parts.at(0)), zone);
            if (!why.isEmpty()) {
                qCWarning(lcEngine).noquote() << "Pads Pro:" << why;
                continue;
            }
            if (zone.loopEnd > zone.left.size() || zone.loopStart >= zone.loopEnd) zone.loopStart = zone.loopEnd = 0;
            set->zones.push_back(std::move(zone));
        }
        if (set->zones.empty()) {
            qCWarning(lcEngine).noquote() << "Pads Pro: no notes could be read for" << name << "- its pads play their synth instead";
            set.reset();
        }
    }
    loaded.emplace(name, set);
    return set;
}

std::shared_ptr<const WaveSet> buildWaves(Engine engine, const EnginePatch& patch)
{
    if (engine != Engine::Vector) return nullptr;
    auto set = std::make_shared<WaveSet>();
    for (const VectorWave w : patch.vector.waves) {
        std::vector<double> wave(kWaveLength, 0.0);
        for (int n = 1; n <= 40; ++n) {
            const double a = vectorHarmonic(w, n);
            if (a <= 0.0) continue;
            for (int i = 0; i < kWaveLength; ++i) wave.at(static_cast<std::size_t>(i)) += a * std::sin(kTau * n * i / kWaveLength);
        }
        const double sum = std::accumulate(wave.begin(), wave.end(), 0.0, [](double total, double x) { return total + (x * x); });
        const double scale = sum > 0.0 ? 0.5 / std::sqrt(sum / kWaveLength) : 0.0; // (each as loud: the crossfades stay level)
        std::vector<float> out(kWaveLength);
        std::ranges::transform(wave, out.begin(), [scale](double x) { return static_cast<float>(x * scale); });
        set->waves.push_back(std::move(out));
    }
    return set;
}

bool engineSpectrum(Engine engine, Spectrum& spectrum, double& bandwidthCents)
{
    switch (engine) {
    case Engine::Piano: spectrum = Spectrum::Piano; bandwidthCents = 1.5; return true; // (three strings a note: their slow beating)
    case Engine::PipeOrgan: spectrum = Spectrum::Principal; bandwidthCents = 1.0; return true;
    case Engine::Harmonium: spectrum = Spectrum::Reed; bandwidthCents = 2.0; return true;
    case Engine::Mellotron: spectrum = Spectrum::Choir; bandwidthCents = 22.0; return true;
    default: return false;
    }
}

void EngineVoice::Line::clear()
{
    std::ranges::fill(data, 0.0F);
    pos = 0;
}

void EngineVoice::Line::push(float x)
{
    pos = pos + 1 >= data.size() ? 0 : pos + 1;
    data.at(pos) = x;
}

float EngineVoice::Line::read(double delay) const
{
    const auto size = static_cast<double>(data.size());
    const double d = std::clamp(delay, 1.0, size - 2.0);
    double at = static_cast<double>(pos) - d;
    if (at < 0.0) at += size;
    const auto i = static_cast<std::size_t>(at);
    const double f = at - static_cast<double>(i);
    const float a = data.at(i % data.size());
    const float b = data.at((i + 1) % data.size());
    return static_cast<float>(a + (b - a) * f);
}

void EngineVoice::allocate(double rate)
{
    const auto length = static_cast<std::size_t>(rate / 25.0) + 16; // (down to about G0)
    for (Line& l : m_lines) l.data.assign(length, 0.0F);
    for (Bow& b : m_bows) {
        b.neck.data.assign(length, 0.0F);
        b.bridge.data.assign(length, 0.0F);
    }
    (void)sine(0.0); // (the table built now, not on the audio thread)
}

void EngineVoice::start(Engine engine, const EnginePatch& p, const EngineContext& c, Random& random) noexcept
{
    m_time = 0.0;
    m_dt = 1.0 / c.rate;
    m_countdown = 0;
    m_ctl.fill(0.0);
    m_state.fill(0.0);
    m_opOut.fill(0.0);
    m_opPrev.fill(0.0);
    std::ranges::generate(m_phase, [&random] { return static_cast<double>(random.unit()); });
    for (Svf& f : m_svf) f.reset();
    std::ranges::fill(m_ladder, LadderFilter{});
    const double controlRate = c.rate / kControl;
    switch (engine) {
    case Engine::Piano:
    case Engine::PipeOrgan:
    case Engine::Harmonium:
    case Engine::Mellotron:
        if (c.spectral != nullptr) {
            m_table = c.spectral->pick(c.hz);
            m_readL = static_cast<double>(random.unit()) * SpectralTables::kLength;
            m_readR = advance(m_readL, SpectralTables::kLength / 2.0);
            // The organ's ranks: each its own place in its own loop.
            for (std::size_t r = 0; r < kRankRatio.size(); ++r) m_phase.at(r) = static_cast<double>(random.unit()) * SpectralTables::kLength;
        }
        if (engine == Engine::PipeOrgan) m_svf.at(2).set(c.hz * 4.0, 1.2, c.rate); // the chiff's band
        break;
    case Engine::Fm:
        for (std::size_t i = 0; i < p.fm.ops.size(); ++i) {
            const FmOperator& op = p.fm.ops.at(i);
            m_env.at(i).set(op.attack, op.decay, op.sustain, op.release, c.rate);
            m_env.at(i).level = 0.0F;
            m_env.at(i).gateOn();
            m_phase.at(i) = 0.0;
        }
        break;
    case Engine::Moog:
        m_env.at(0).set(p.moog.envAttack, p.moog.envDecay, p.moog.envSustain, 4.0, controlRate);
        m_env.at(0).level = 0.0F;
        m_env.at(0).gateOn();
        break;
    case Engine::Juno:
        m_env.at(0).set(p.juno.envAttack, p.juno.envDecay, p.juno.envSustain, 4.0, controlRate);
        m_env.at(0).level = 0.0F;
        m_env.at(0).gateOn();
        break;
    case Engine::TwinAnalog:
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const AnalogChannel& a = ch == 0 ? p.twin.one : p.twin.two;
            m_env.at(ch).set(a.envAttack, a.envDecay, a.envSustain, 4.0, controlRate);
            m_env.at(ch).level = 0.0F;
            m_env.at(ch).gateOn();
        }
        break;
    case Engine::Supersaw:
        m_env.at(0).set(p.supersaw.envAttack, p.supersaw.envDecay, p.supersaw.envSustain, 4.0, controlRate);
        m_env.at(0).level = 0.0F;
        m_env.at(0).gateOn();
        break;
    case Engine::Ebow:
    case Engine::Strum:
    case Engine::Flute:
        for (Line& l : m_lines) l.clear();
        m_nextStrike = 0.0;
        break;
    case Engine::Bowed:
        for (std::size_t j = 0; j < m_bows.size(); ++j) {
            Bow& b = m_bows.at(j);
            b.neck.clear();
            b.bridge.clear();
            b.loss = 0.0;
            b.vibrato = random.unit();
            b.vibratoHz = p.bowed.vibratoHz * (0.85 + 0.3 * random.unit());
            b.detune = j == 0 ? 0.0 : (j == 1 ? p.bowed.detuneCents : -p.bowed.detuneCents);
        }
        // The violin's body: its air and wood resonances and the bridge's hill.
        m_svf.at(3).set(280.0, 2.0, c.rate);
        m_svf.at(4).set(460.0, 3.0, c.rate);
        m_svf.at(5).set(2800.0, 1.2, c.rate);
        m_svf.at(6).set(280.0, 2.0, c.rate);
        m_svf.at(7).set(2800.0, 1.2, c.rate);
        break;
    case Engine::Modal:
        for (std::size_t m = 0; m < m_modeC1.size(); ++m) {
            const double hz = c.hz * p.modal.ratios.at(m);
            m_modeY1.at(m) = 0.0;
            m_modeY2.at(m) = 0.0;
            if (hz >= 0.45 * c.rate || p.modal.gains.at(m) <= 0.0) {
                m_modeGain.at(m) = 0.0;
                continue;
            }
            const double w = kTau * hz / c.rate;
            const double r = std::exp(-1.0 / (p.modal.decays.at(m) * c.rate));
            m_modeC1.at(m) = 2.0 * r * std::cos(w);
            m_modeC2.at(m) = -r * r;
            // (Rubbed by noise: each mode as loud, whatever its pitch and ring:
            // a resonator's noise output grows as 1 / (2 sin w sqrt(1 - r)).)
            m_modeGain.at(m) = p.modal.gains.at(m) * 2.0 * std::sin(w) * std::sqrt(1.0 - r);
        }
        m_svf.at(0).set(c.hz * 2.0, 0.8, c.rate);
        m_nextStrike = 0.0;
        break;
    case Engine::Granular:
        for (Grain& g : m_grains) g.on = false;
        m_nextGrain = 0.0;
        break;
    case Engine::Tine:
        for (std::size_t i = 0; i < 3; ++i) m_phase.at(i) = 0.0;
        break;
    case Engine::Sampled:
        if (c.samples != nullptr && !c.samples->zones.empty()) {
            m_zone = c.samples->pick(static_cast<int>(std::lround(12.0 * std::log2(c.hz / 440.0) + 69.0)));
            m_samplePos = 0.0;
        }
        break;
    case Engine::Wind:
    case Engine::Ocean:
    case Engine::Forest:
    case Engine::Cave:
        for (Grain& g : m_grains) g.on = false;
        m_nextStrike = 0.0;
        m_nextGrain = 0.0;
        m_ctl.at(0) = m_ctl.at(1) = 0.5;  // (wind and leaves: a middling gust)
        m_ctl.at(3) = random.unit() * 0.4; // (the sea: each ear's wave somewhere along)
        m_ctl.at(4) = 7.0 + 3.0 * random.unit();
        m_ctl.at(5) = 0.5 + random.unit() * 0.4;
        m_ctl.at(6) = 7.0 + 3.0 * random.unit();
        if (engine == Engine::Ocean) m_svf.at(2).set(c.hz * 2.0, 15.0, c.rate);
        if (engine == Engine::Cave) {
            m_svf.at(0).set(70.0, 0.7, c.rate);
            m_svf.at(1).set(70.0, 0.7, c.rate);
        }
        break;
    default: break;
    }
}

void EngineVoice::retrigger() noexcept
{
    // A recording starts again from its beginning (a piano struck again, a
    // bow drawn again), the old sound faded out under it.
    m_state.at(0) = m_samplePos;
    m_state.at(1) = 1.0;
    m_samplePos = 0.0;
    m_time = 0.0;
    m_ctl.at(15) = 0.0; // (not let go)
    for (sawstack::Envelope& e : m_env) e.gateOn();
    m_nextStrike = 0.0;
    m_nextGrain = 0.0;
}

// A few times a millisecond: what moves (filters, envelopes, paths, pitches).
void EngineVoice::control(Engine engine, const EnginePatch& p, const EngineContext& c, Random& random) noexcept
{
    const double t = m_time;
    const double bright = std::exp2(c.brightness);
    const double keyOctaves = std::log2(c.hz / 261.63);
    const bool releasing = !c.held && m_ctl.at(15) == 0.0;
    if (releasing) m_ctl.at(15) = 1.0;
    switch (engine) {
    case Engine::Piano: {
        const PianoSettings& s = p.piano;
        const double hammer = std::exp(-t / s.hammerSeconds);
        double amp = s.settle + (1.0 - s.settle) * std::exp(-t / (s.hammerSeconds * 1.5));
        if (s.bloomSeconds > 0.0) amp *= 0.55 + 0.45 * smooth(t / s.bloomSeconds);
        m_ctl.at(0) = amp;
        const double cutoff = s.lpHz * bright * std::exp2(s.hammerBright * hammer + 0.6 * c.character + 0.3 * keyOctaves);
        m_svf.at(0).set(cutoff, 0.6, c.rate);
        m_svf.at(1).set(cutoff, 0.6, c.rate);
        if (c.spectral != nullptr) {
            const auto table = static_cast<std::size_t>(m_table);
            m_readStep = c.hz / c.spectral->baseHz.at(table) * (c.spectral->sampleRate / c.rate);
        }
        break;
    }
    case Engine::PipeOrgan:
    case Engine::Harmonium:
    case Engine::Mellotron: {
        if (c.spectral == nullptr) break;
        const auto table = static_cast<std::size_t>(m_table);
        const double base = c.hz / c.spectral->baseHz.at(table) * (c.spectral->sampleRate / c.rate);
        if (engine == Engine::Harmonium) {
            const HarmoniumSettings& s = p.harmonium;
            const double breath = s.bellows * c.motion * std::sin(kTau * s.bellowsHz * t);
            m_ctl.at(0) = 1.0 + 0.5 * breath;
            m_ctl.at(1) = base * cents(0.5 * s.celeste + 2.0 * breath);
            m_ctl.at(2) = base * cents(-0.5 * s.celeste + 2.0 * breath);
            const double cutoff = s.lpHz * bright * std::exp2(0.6 * c.character + 0.3 * breath);
            m_svf.at(0).set(cutoff, 0.7, c.rate);
            m_svf.at(1).set(cutoff, 0.7, c.rate);
        } else if (engine == Engine::Mellotron) {
            const MellotronSettings& s = p.mellotron;
            const double wow = s.wow * c.motion * std::sin(kTau * s.wowHz * t);
            m_state.at(0) += (random.bipolar() - m_state.at(0)) * 0.02; // the flutter's own wander
            const double flutter = s.flutter * (std::sin(kTau * 9.0 * t) * 0.6 + m_state.at(0));
            m_ctl.at(1) = base * cents(wow + flutter);
            const double cutoff = s.toneHz * bright * std::exp2(0.5 * c.character);
            m_svf.at(0).set(cutoff, 0.6, c.rate);
            m_svf.at(1).set(cutoff, 0.6, c.rate);
            m_svf.at(2).set(cutoff, 1.3, c.rate);
            m_svf.at(3).set(cutoff, 1.3, c.rate);
        } else {
            const PipeOrganSettings& s = p.pipe;
            const double trem = 1.0 + 0.12 * s.tremulant * c.motion * std::sin(kTau * 6.2 * t);
            m_ctl.at(0) = trem;
            for (std::size_t r = 0; r < kRankRatio.size(); ++r) {
                const double hz = c.hz * kRankRatio.at(r);
                const int rankTable = c.spectral->pick(hz);
                m_ctl.at(4 + r) = rankTable;
                m_step.at(r) = hz / c.spectral->baseHz.at(static_cast<std::size_t>(rankTable)) * (c.spectral->sampleRate / c.rate);
            }
            const double cutoff = 6000.0 * bright * std::exp2(0.6 * c.character);
            m_svf.at(0).set(cutoff, 0.6, c.rate);
            m_svf.at(1).set(cutoff, 0.6, c.rate);
        }
        break;
    }
    case Engine::Tine: {
        const TineSettings& s = p.tine;
        m_ctl.at(0) = (s.indexSustain + (s.index - s.indexSustain) * std::exp(-t / s.indexSeconds)) * std::exp2(c.character);
        m_ctl.at(1) = s.bell * std::exp(-t / 0.3);
        m_ctl.at(2) = s.tremDepth * c.motion * std::sin(kTau * s.tremHz * t);
        m_step.at(0) = c.hz / c.rate;
        m_step.at(1) = c.hz / c.rate;
        m_step.at(2) = c.hz * 14.0 / c.rate;
        break;
    }
    case Engine::Ebow: {
        const EbowSettings& s = p.ebow;
        const double vib = s.vibratoCents * c.motion * std::sin(kTau * s.vibratoHz * t) * smooth((t - 0.8) / 1.5);
        m_ctl.at(0) = c.rate / (c.hz * cents(vib + 3.0)) - 0.5;
        m_ctl.at(1) = c.rate / (c.hz * cents(vib - 3.0)) - 0.5;
        m_ctl.at(2) = c.held ? smooth(t / s.swellSeconds) * s.drive * std::exp2(0.5 * c.character) : 0.0;
        const double tone = s.toneHz * bright;
        m_svf.at(0).set(tone, 0.7, c.rate);
        m_svf.at(1).set(tone, 0.7, c.rate);
        break;
    }
    case Engine::Strum: {
        const StrumSettings& s = p.strum;
        m_ctl.at(0) = c.rate / c.hz - 0.5;
        m_ctl.at(1) = c.rate / (c.hz * 2.0) - 0.5;
        m_ctl.at(2) = std::pow(10.0, -3.0 / (s.decay * c.hz));       // each trip round the string
        m_ctl.at(3) = std::pow(10.0, -3.0 / (s.decay * c.hz * 2.0));
        const double tone = s.toneHz * bright;
        m_svf.at(0).set(tone, 0.7, c.rate);
        m_svf.at(1).set(tone, 0.7, c.rate);
        // Strike again while held: both strings, each strike its own strength.
        if (c.held && t >= m_nextStrike) {
            const double strength = 0.6 + 0.4 * random.unit();
            const double b = std::clamp(0.08 + 0.85 * s.brightness * std::exp2(0.7 * c.character), 0.02, 1.0);
            for (std::size_t k = 0; k < 2; ++k) {
                Line& line = m_lines.at(k);
                const auto length = static_cast<std::size_t>(m_ctl.at(k));
                const double level = strength * (k == 0 ? 1.0 : s.octave);
                double lp = 0.0;
                for (std::size_t i = 0; i <= length && i < line.data.size(); ++i) {
                    lp += b * (random.bipolar() - lp);
                    const std::size_t at = (line.pos + line.data.size() - i) % line.data.size();
                    line.data.at(at) += static_cast<float>(level * lp);
                }
            }
            const double gap = 1.0 / (s.strumHz * std::max(c.motion, 0.25));
            m_nextStrike = t + gap * (1.0 + s.jitter * random.bipolar());
        }
        break;
    }
    case Engine::Bowed: {
        const BowedSettings& s = p.bowed;
        for (Bow& b : m_bows) b.vibrato = wrap(b.vibrato + b.vibratoHz * m_dt * kControl);
        const double in = smooth((t - 0.4) / 1.2); // (the vibrato comes in after the bow)
        for (std::size_t j = 0; j < m_bows.size(); ++j) {
            const Bow& b = m_bows.at(j);
            const double vib = s.vibratoCents * c.motion * in * std::sin(kTau * b.vibrato);
            m_ctl.at(j) = c.rate / (c.hz * cents(b.detune + vib)) - 1.0;
        }
        const double target = c.held ? (0.03 + 0.17 * std::exp2(0.5 * c.character)) : 0.0;
        m_ctl.at(4) += (target - m_ctl.at(4)) * (c.held ? 0.004 : 0.01); // the bow's speed, coming in and going
        break;
    }
    case Engine::StringMachine: {
        const StringMachineSettings& s = p.strings;
        m_step.at(0) = c.hz * 0.5 / c.rate;
        m_svf.at(0).set(550.0, 1.5, c.rate);
        m_svf.at(1).set(1900.0, 2.0, c.rate);
        m_svf.at(2).set(s.lpHz * bright * std::exp2(0.6 * c.character), 0.6, c.rate);
        break;
    }
    case Engine::Tonewheel: {
        for (std::size_t k = 0; k < kFootage.size(); ++k) m_step.at(k) = c.hz * kFootage.at(k) / c.rate;
        break;
    }
    case Engine::Moog: {
        const MoogSettings& s = p.moog;
        const double env = m_env.at(0).tick();
        if (releasing) m_env.at(0).gateOff();
        const double cutoff = s.cutoffHz * bright * std::exp2(s.envOctaves * env + s.keyTrack * keyOctaves + c.character);
        m_coeffs.at(0).set(cutoff, s.resonance, c.rate);
        for (std::size_t k = 0; k < 3; ++k) m_step.at(k) = c.hz * std::exp2(s.octave.at(k)) * cents(s.detuneCents.at(k)) / c.rate;
        break;
    }
    case Engine::Juno: {
        const JunoSettings& s = p.juno;
        const double env = m_env.at(0).tick();
        if (releasing) m_env.at(0).gateOff();
        const double cutoff = s.cutoffHz * bright * std::exp2(s.envOctaves * env + s.keyTrack * keyOctaves + c.character);
        m_coeffs.at(0).set(cutoff, s.resonance, c.rate);
        m_svf.at(0).set(s.hpHz, 0.7, c.rate);
        m_ctl.at(0) = std::clamp(s.pulseWidth + s.pwmDepth * 0.45 * c.motion * std::sin(kTau * s.lfoHz * t), 0.05, 0.95);
        m_step.at(0) = c.hz / c.rate;
        break;
    }
    case Engine::TwinAnalog: {
        const double vib = p.twin.vibratoCents * c.motion * std::sin(kTau * p.twin.vibratoHz * t);
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const AnalogChannel& a = ch == 0 ? p.twin.one : p.twin.two;
            const double env = m_env.at(ch).tick();
            if (releasing) m_env.at(ch).gateOff();
            const double cutoff = a.lpHz * bright * std::exp2(a.envOctaves * env + 0.4 * keyOctaves + c.character);
            m_coeffs.at(ch).set(cutoff, a.resonance, c.rate);
            m_svf.at(ch).set(a.hpHz, 0.7, c.rate);
            m_ctl.at(ch) = std::clamp(a.pulseWidth + a.pwmDepth * c.motion * std::sin(kTau * a.pwmHz * t + static_cast<double>(ch)), 0.05, 0.95);
            m_step.at(ch) = c.hz * std::exp2(a.octave) * cents(a.detuneCents + vib) / c.rate;
        }
        break;
    }
    case Engine::Vector: {
        const VectorSettings& s = p.vector;
        const double along = t / (s.segmentSeconds / std::max(c.motion, 0.1));
        const auto seg = static_cast<std::size_t>(std::floor(along)) % 4;
        const double f = smooth(along - std::floor(along));
        const std::size_t next = (seg + 1) % 4;
        const double x = std::clamp(s.pathX.at(seg) + (s.pathX.at(next) - s.pathX.at(seg)) * f + 0.35 * c.character, 0.0, 1.0);
        const double y = std::clamp(s.pathY.at(seg) + (s.pathY.at(next) - s.pathY.at(seg)) * f, 0.0, 1.0);
        m_ctl.at(0) = (1.0 - x) * (1.0 - y);
        m_ctl.at(1) = x * (1.0 - y);
        m_ctl.at(2) = (1.0 - x) * y;
        m_ctl.at(3) = x * y;
        m_step.at(0) = c.hz * cents(s.detuneCents) / c.rate;
        m_step.at(1) = c.hz * cents(-s.detuneCents) / c.rate;
        const double cutoff = s.cutoffHz * bright;
        m_svf.at(0).set(cutoff, 0.7 + 3.0 * s.resonance, c.rate);
        m_svf.at(1).set(cutoff, 0.7 + 3.0 * s.resonance, c.rate);
        break;
    }
    case Engine::Fm: {
        const FmSettings& s = p.fm;
        m_ctl.at(0) = std::exp2(c.character) * (1.0 + s.lfoDepth * c.motion * std::sin(kTau * s.lfoHz * t));
        for (std::size_t i = 0; i < s.ops.size(); ++i) {
            m_step.at(i) = (c.hz * s.ops.at(i).ratio + s.ops.at(i).detuneHz) / c.rate;
            if (releasing) m_env.at(i).gateOff();
        }
        break;
    }
    case Engine::Supersaw: {
        const SupersawSettings& s = p.supersaw;
        const double env = m_env.at(0).tick();
        if (releasing) m_env.at(0).gateOff();
        const double cutoff = s.cutoffHz * bright * std::exp2(s.envOctaves * env + 0.4 * keyOctaves + c.character);
        m_coeffs.at(0).set(cutoff, s.resonance, c.rate);
        m_svf.at(0).set(c.hz, 0.7, c.rate); // (Szabo: the JP's high-pass follows the note)
        m_svf.at(1).set(c.hz, 0.7, c.rate);
        const double spread = 0.6 * s.detune * std::exp2(0.5 * c.character);
        for (std::size_t k = 0; k < 7; ++k) m_step.at(k) = c.hz * (1.0 + sawstack::kMeasuredOffsets.at(k) * spread) / c.rate;
        break;
    }
    case Engine::Flute: {
        const FluteSettings& s = p.flute;
        const double vib = s.vibratoDepth * c.motion * std::sin(kTau * s.vibratoHz * t) * smooth((t - 0.5) / 1.0);
        m_ctl.at(0) = c.rate / c.hz - 2.0;                                      // the bore
        m_ctl.at(1) = m_ctl.at(0) * s.jet * std::exp2(-0.25 * c.character);        // the jet
        const double target = c.held ? s.pressure : 0.0;
        m_ctl.at(2) += (target - m_ctl.at(2)) * (c.held ? 0.01 : 0.02);
        m_ctl.at(3) = vib;
        break;
    }
    case Engine::Modal: {
        const ModalSettings& s = p.modal;
        const double target = c.held ? s.rub * std::exp2(0.5 * c.character) : 0.0;
        m_ctl.at(0) += (target - m_ctl.at(0)) * 0.01;
        m_ctl.at(1) = 0.0;
        if (c.held && t >= m_nextStrike && s.strikeHz > 0.0) {
            m_ctl.at(1) = 0.6 + 0.4 * random.unit();
            m_nextStrike = t + (0.5 + random.unit()) / (s.strikeHz * std::max(c.motion, 0.2));
        }
        break;
    }
    case Engine::Granular: {
        const GranularSettings& s = p.granular;
        m_svf.at(0).set(s.cutoffHz * bright, 0.7, c.rate);
        m_svf.at(1).set(s.cutoffHz * bright, 0.7, c.rate);
        const double density = s.density * std::exp2(c.character) * std::max(c.motion, 0.2);
        const double weights = std::accumulate(s.weights.begin(), s.weights.end(), 0.0);
        const double overlap = std::max(1.0, density * (s.grainMs + 0.5 * s.spreadMs) / 1000.0);
        while (c.held && t >= m_nextGrain) {
            m_nextGrain += (0.5 + random.unit()) / density;
            auto free = std::ranges::find_if(m_grains, [](const Grain& g) { return !g.on; });
            if (free == m_grains.end()) continue;
            double pick = random.unit() * weights;
            std::size_t k = 0;
            while (k + 1 < s.weights.size() && pick > s.weights.at(k)) {
                pick -= s.weights.at(k);
                ++k;
            }
            const double ratio = s.partials.at(k) * (random.unit() < s.octaveUp ? 2.0 : 1.0) * cents(s.detuneCents * random.bipolar());
            const double hz = c.hz * ratio;
            if (hz >= 0.4 * c.rate) continue;
            Grain& g = *free;
            g.on = true;
            g.phase = random.unit();
            g.step = hz / c.rate;
            g.at = 0.0;
            g.length = (s.grainMs + s.spreadMs * random.unit()) / 1000.0 * c.rate;
            g.gain = s.weights.at(k) / weights * 2.5 * (0.6 + 0.4 * random.unit()) / std::sqrt(overlap);
            const double place = random.unit() * kPi * 0.5;
            g.left = std::cos(place) * std::numbers::sqrt2;
            g.right = std::sin(place) * std::numbers::sqrt2;
            g.toneSin = std::sin(2.0 * kPi * g.phase);
            g.toneCos = std::cos(2.0 * kPi * g.phase);
            g.toneStepSin = std::sin(2.0 * kPi * g.step);
            g.toneStepCos = std::cos(2.0 * kPi * g.step);
            g.windowSin = 0.0; // (its window: half a sine over its length)
            g.windowCos = 1.0;
            g.windowStepSin = std::sin(kPi / g.length);
            g.windowStepCos = std::cos(kPi / g.length);
        }
        if (!c.held && m_nextGrain < t) m_nextGrain = t; // (let go: no burst when held again)
        break;
    }
    case Engine::Wind:
    case Engine::Ocean:
    case Engine::Forest:
    case Engine::Cave: {
        const PlaceSettings& s = p.place;
        const double busy = s.busy * std::max(c.motion, 0.2);
        const double tick = kControl * m_dt;
        // Gusts: each ear its own, gliding to a new strength every few seconds.
        if (t >= m_ctl.at(11)) {
            m_ctl.at(2) = random.unit();
            m_ctl.at(12) = random.unit();
            m_ctl.at(11) = t + (1.5 + 3.0 * random.unit()) / busy;
        }
        const double glide = 1.0 - std::exp(-tick / 0.8);
        m_ctl.at(0) += (m_ctl.at(2) - m_ctl.at(0)) * glide;
        m_ctl.at(1) += (m_ctl.at(12) - m_ctl.at(1)) * glide;
        m_ctl.at(13) += (random.unit() - m_ctl.at(13)) * 0.3; // (leaves' flutter)
        if (engine == Engine::Wind) {
            // The howl: the note's first harmonics, bending with the gusts.
            const double bend = cents(40.0 * (m_ctl.at(0) - 0.5));
            m_svf.at(0).set(c.hz * bend, 25.0, c.rate);
            m_svf.at(1).set(c.hz * 2.0 * bend, 30.0, c.rate);
            m_svf.at(2).set(std::min(c.hz * 3.0 * bend, 0.4 * c.rate), 35.0, c.rate);
            m_svf.at(3).set((200.0 + 1300.0 * m_ctl.at(0)) * bright, 0.6, c.rate);
            m_svf.at(4).set((200.0 + 1300.0 * m_ctl.at(1)) * bright, 0.6, c.rate);
        } else if (engine == Engine::Ocean) {
            // Each ear its own waves, rolling in, breaking, running back.
            for (std::size_t ear = 0; ear < 2; ++ear) {
                double& pos = m_ctl.at(3 + 2 * ear);
                double& length = m_ctl.at(4 + 2 * ear);
                pos += tick / length;
                if (pos >= 1.0) {
                    pos -= 1.0;
                    length = (6.0 + 5.0 * random.unit()) / busy;
                }
                m_ctl.at(7 + ear) = waveSwell(pos);
                const double crash = waveCrash(pos);
                m_svf.at(ear).set((200.0 + 500.0 * m_ctl.at(7 + ear) + 2500.0 * crash) * bright, 0.6, c.rate);
            }
            m_step.at(0) = c.hz / c.rate;
            m_step.at(1) = c.hz * 1.5 / c.rate;
            m_step.at(2) = c.hz * 2.0 / c.rate;
        } else if (engine == Engine::Forest) {
            m_svf.at(0).set(2500.0 * bright, 0.7, c.rate);
            m_svf.at(1).set(2500.0 * bright, 0.7, c.rate);
            m_step.at(0) = c.hz / c.rate;
            m_step.at(1) = c.hz * 1.5 / c.rate;
        } else if (engine == Engine::Cave) {
            m_step.at(0) = c.hz * 0.5 / c.rate;
            m_step.at(1) = c.hz * 0.5 * cents(3.0) / c.rate;
            m_step.at(2) = c.hz * 0.75 / c.rate;
        }
        break;
    }
    case Engine::Sampled: {
        const SampledSettings& s = p.sampled;
        if (c.samples == nullptr || m_zone >= c.samples->zones.size()) break;
        const SampleZone& z = c.samples->zones.at(m_zone);
        // Played at the note: its own speed against the note it was recorded at.
        m_sampleStep = c.hz / (440.0 * std::exp2((z.root - 69) / 12.0)) * (z.rate / c.rate) * std::exp2(s.tuneCents / 1200.0);
        const double tone = s.toneHz * bright * std::exp2(0.7 * c.character);
        m_svf.at(0).set(tone, 0.6, c.rate);
        m_svf.at(1).set(tone, 0.6, c.rate);
        m_ctl.at(0) = s.bloomSeconds > 0.0 ? 0.55 + 0.45 * smooth(t / s.bloomSeconds) : 1.0;
        break;
    }
    case Engine::Classic: break;
    }
}

void EngineVoice::render(Engine engine, const EnginePatch& p, const EngineContext& c, Random& random, std::span<float> left,
                         std::span<float> right) noexcept
{
    const std::size_t n = std::min(left.size(), right.size());
    auto toLeft = left.begin();
    auto toRight = right.begin();
    for (std::size_t i = 0; i < n; ++i) {
        if (--m_countdown <= 0) {
            m_countdown = kControl;
            control(engine, p, c, random);
        }
        double l = 0.0;
        double r = 0.0;
        switch (engine) {
        case Engine::Piano: {
            if (c.spectral == nullptr) break;
            const std::vector<float>& loop = c.spectral->loops.at(static_cast<std::size_t>(m_table));
            const double a = loopRead(loop, m_readL);
            const double b = loopRead(loop, m_readR);
            m_readL = advance(m_readL, m_readStep);
            m_readR = advance(m_readR, m_readStep);
            // The hammer's thump, under the first moments.
            const double thump = random.bipolar() * 0.25 * std::exp(-m_time / 0.012);
            l = m_svf.at(0).lowPass(static_cast<float>(a + thump)) * m_ctl.at(0);
            r = m_svf.at(1).lowPass(static_cast<float>(0.55 * a + 0.45 * b + thump) * 1.35F) * m_ctl.at(0);
            break;
        }
        case Engine::Harmonium: {
            if (c.spectral == nullptr) break;
            const std::vector<float>& loop = c.spectral->loops.at(static_cast<std::size_t>(m_table));
            const double a = loopRead(loop, m_readL);
            const double b = loopRead(loop, m_readR);
            m_readL = advance(m_readL, m_ctl.at(1));
            m_readR = advance(m_readR, m_ctl.at(2));
            l = m_svf.at(0).lowPass(static_cast<float>((a + 0.7 * b) * m_ctl.at(0) * 0.8));
            r = m_svf.at(1).lowPass(static_cast<float>((b + 0.7 * a) * m_ctl.at(0) * 0.8));
            break;
        }
        case Engine::Mellotron: {
            if (c.spectral == nullptr) break;
            const std::vector<float>& loop = c.spectral->loops.at(static_cast<std::size_t>(m_table));
            const double a = loopRead(loop, m_readL); // (one tape: mono)
            m_readL = advance(m_readL, m_ctl.at(1));
            const double hiss = p.mellotron.hiss * random.bipolar();
            l = m_svf.at(2).lowPass(m_svf.at(0).lowPass(static_cast<float>(a + hiss)));
            r = m_svf.at(3).lowPass(m_svf.at(1).lowPass(static_cast<float>(a + p.mellotron.hiss * random.bipolar())));
            break;
        }
        case Engine::PipeOrgan: {
            if (c.spectral == nullptr) break;
            const PipeOrganSettings& s = p.pipe;
            double sumL = 0.0;
            double sumR = 0.0;
            for (std::size_t k = 0; k < kRankRatio.size(); ++k) {
                if (s.ranks.at(k) <= 0.0) continue;
                const auto table = static_cast<std::size_t>(m_ctl.at(4 + k));
                const double x = loopRead(c.spectral->loops.at(table), m_phase.at(k)) * s.ranks.at(k);
                m_phase.at(k) = advance(m_phase.at(k), m_step.at(k));
                // (The ranks stand in their own places, as pipes do.)
                const double side = k % 2 == 0 ? 0.35 : -0.35;
                sumL += x * (1.0 + side);
                sumR += x * (1.0 - side);
            }
            const double air = random.bipolar();
            const double chiff = bandPass(m_svf.at(2), air) * s.chiff * std::exp(-m_time / 0.07) * 0.6;
            const double wind = air * s.wind;
            l = m_svf.at(0).lowPass(static_cast<float>((sumL + chiff + wind) * m_ctl.at(0) * 0.6));
            r = m_svf.at(1).lowPass(static_cast<float>((sumR + chiff - wind) * m_ctl.at(0) * 0.6));
            break;
        }
        case Engine::Tine: {
            const TineSettings& s = p.tine;
            m_phase.at(0) = wrap(m_phase.at(0) + m_step.at(0));
            m_phase.at(1) = wrap(m_phase.at(1) + m_step.at(1));
            m_phase.at(2) = wrap(m_phase.at(2) + m_step.at(2));
            const double mod = sine(m_phase.at(1)) * m_ctl.at(0) * 0.25 + sine(m_phase.at(2)) * m_ctl.at(1) * 0.08;
            const double car = sine(m_phase.at(0) + mod);
            const double y = (car + s.bark * 0.4 * (car * car - 0.5)) * 0.6; // (the pickup: louder one way than the other)
            l = y * (1.0 + m_ctl.at(2));
            r = y * (1.0 - m_ctl.at(2));
            break;
        }
        case Engine::Ebow: {
            // Two strings (a little apart in tune, one each side), each fed by its own pickup.
            for (std::size_t k = 0; k < 2; ++k) {
                Line& line = m_lines.at(k);
                const double length = m_ctl.at(k);
                const double out = 0.5 * (line.read(length) + line.read(length + 1.0)) * 0.996;
                double& last = m_state.at(k);
                const double drive = m_ctl.at(2) * 0.05 * fastTanh(static_cast<float>(3.0 * last)) + 1e-4 * random.bipolar() * m_ctl.at(2);
                const double y = out + drive;
                line.push(static_cast<float>(y));
                last = y;
                // Heard at the pickup: the string less itself a little further along.
                const double heard = (y - line.read(length * p.ebow.pickup)) * 0.03;
                (k == 0 ? l : r) = m_svf.at(k).lowPass(static_cast<float>(heard));
            }
            break;
        }
        case Engine::Strum: {
            for (std::size_t k = 0; k < 2; ++k) {
                Line& line = m_lines.at(k);
                const double length = m_ctl.at(k);
                const double y = 0.5 * (line.read(length) + line.read(length + 1.0)) * m_ctl.at(2 + k);
                line.push(static_cast<float>(y));
                const double side = k == 0 ? 0.3 : -0.3;
                l += y * (1.0 + side);
                r += y * (1.0 - side);
            }
            l = m_svf.at(0).lowPass(static_cast<float>(l * 2.0));
            r = m_svf.at(1).lowPass(static_cast<float>(r * 2.0));
            break;
        }
        case Engine::Bowed: {
            const BowedSettings& s = p.bowed;
            const double slope = 5.0 - 4.0 * std::clamp(s.pressure, 0.0, 1.0);
            const double pole = 0.75 - 0.2 * 22050.0 / c.rate;
            const int players = std::clamp(s.players, 1, 3);
            for (int j = 0; j < players; ++j) {
                Bow& b = m_bows.at(static_cast<std::size_t>(j));
                const double base = m_ctl.at(static_cast<std::size_t>(j));
                const double bridgeLength = base * s.position;
                const double neckLength = base - bridgeLength;
                const double fromBridge = b.bridge.read(bridgeLength);
                b.loss = (1.0 - pole) * fromBridge + pole * b.loss;
                const double bridgeRefl = -0.95 * b.loss;
                const double nutRefl = -b.neck.read(neckLength);
                const double stringVel = bridgeRefl + nutRefl;
                const double velDiff = m_ctl.at(4) - stringVel;
                const double newVel = velDiff * bowTable(velDiff, slope);
                b.neck.push(static_cast<float>(bridgeRefl + newVel));
                b.bridge.push(static_cast<float>(nutRefl + newVel));
                const double side = j == 1 ? 0.5 : (j == 2 ? -0.5 : 0.0);
                l += fromBridge * (1.0 + side);
                r += fromBridge * (1.0 - side);
            }
            // The violin's body over the strings.
            const double bl = l + s.bodyGain * (bandPass(m_svf.at(3), l) + 0.7 * bandPass(m_svf.at(4), l) + 0.8 * bandPass(m_svf.at(5), l));
            const double br = r + s.bodyGain * (bandPass(m_svf.at(6), r) + 0.7 * bandPass(m_svf.at(4), r) * 0.0 + 0.8 * bandPass(m_svf.at(7), r));
            l = bl * 1.6 / players;
            r = br * 1.6 / players;
            break;
        }
        case Engine::StringMachine: {
            const StringMachineSettings& s = p.strings;
            // The divide-down: every footage from one phase, locked together.
            m_phase.at(0) = wrap(m_phase.at(0) + m_step.at(0));
            const double p16 = m_phase.at(0);
            const double p8 = wrap(2.0 * p16);
            const double p4 = wrap(4.0 * p16);
            const double dt = m_step.at(0);
            const double x = s.sixteen * saw(p16, dt) + s.eight * saw(p8, 2.0 * dt) + s.four * saw(p4, 4.0 * dt);
            const double body = bandPass(m_svf.at(0), x) + 0.6 * bandPass(m_svf.at(1), x);
            const double y = m_svf.at(2).lowPass(static_cast<float>((1.0 - s.body) * x + s.body * 1.5 * body)) * 0.35;
            l = y;
            r = y;
            break;
        }
        case Engine::Tonewheel: {
            const TonewheelSettings& s = p.tonewheel;
            double sum = 0.0;
            for (std::size_t k = 0; k < kFootage.size(); ++k) {
                const double bar = s.drawbars.at(k);
                m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
                if (bar <= 0.0) continue;
                double level = std::pow(10.0, -3.0 * (8.0 - bar) / 20.0); // (each step of a drawbar: 3 dB)
                if (k == 3) level += s.percussion * std::exp(-m_time / 0.35);
                sum += sine(m_phase.at(k)) * level;
            }
            const double click = random.bipolar() * s.click * std::exp(-m_time / 0.004);
            l = (sum * 0.22) + click;
            r = l;
            break;
        }
        case Engine::Moog: {
            const MoogSettings& s = p.moog;
            double sumL = 0.0;
            double sumR = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
                const double ph = m_phase.at(k);
                const double dt = m_step.at(k);
                double x = 0.0;
                switch (s.wave.at(k)) {
                case 1: x = triangle(ph); break;
                case 2: x = pulse(ph, 0.5, dt); break;
                default: x = saw(ph, dt); break;
                }
                x *= s.level.at(k);
                const double side = k == 1 ? 0.3 : (k == 2 ? -0.3 : 0.0);
                sumL += x * (1.0 + side);
                sumR += x * (1.0 - side);
            }
            const auto drive = static_cast<float>(1.0 + 3.0 * s.drive);
            l = m_ladder.at(0).tick(static_cast<float>(sumL * 0.3), m_coeffs.at(0), drive);
            r = m_ladder.at(1).tick(static_cast<float>(sumR * 0.3), m_coeffs.at(0), drive);
            break;
        }
        case Engine::Juno: {
            const JunoSettings& s = p.juno;
            const double before = m_phase.at(0);
            m_phase.at(0) = wrap(m_phase.at(0) + m_step.at(0));
            if (m_phase.at(0) < before) m_state.at(0) = m_state.at(0) > 0.0 ? -1.0 : 1.0; // (the sub flips each cycle: an octave down)
            const double ph = m_phase.at(0);
            const double dt = m_step.at(0);
            const double sub = m_state.at(0) == 0.0 ? 1.0 : m_state.at(0);
            const double x = s.pulse * pulse(ph, m_ctl.at(0), dt) + s.saw * saw(ph, dt) + s.sub * sub + s.noise * random.bipolar();
            const double hp = m_svf.at(0).highPass(static_cast<float>(x * 0.3));
            l = m_ladder.at(0).tick(static_cast<float>(hp), m_coeffs.at(0), 1.2F);
            r = l;
            break;
        }
        case Engine::TwinAnalog: {
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const AnalogChannel& a = ch == 0 ? p.twin.one : p.twin.two;
                m_phase.at(ch) = wrap(m_phase.at(ch) + m_step.at(ch));
                const double ph = m_phase.at(ch);
                const double dt = m_step.at(ch);
                const double x = a.saw * saw(ph, dt) + a.square * pulse(ph, m_ctl.at(ch), dt);
                const double hp = m_svf.at(ch).highPass(static_cast<float>(x * 0.3));
                double y = m_ladder.at(ch).tick(static_cast<float>(hp), m_coeffs.at(ch), 1.2F);
                y += a.sine * sine(ph) * 0.3; // (the sine after the filter, as the CS-80's)
                y *= a.level;
                l += y * (1.0 - a.pan);
                r += y * (1.0 + a.pan);
            }
            break;
        }
        case Engine::Vector: {
            if (c.waves == nullptr || c.waves->waves.size() < 4) break;
            std::array<double, 2> copies{};
            for (std::size_t k = 0; k < 2; ++k) {
                m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
                double x = 0.0;
                for (std::size_t w = 0; w < 4; ++w) x += m_ctl.at(w) * waveRead(c.waves->waves.at(w), m_phase.at(k));
                copies.at(k) = x;
            }
            l = m_svf.at(0).lowPass(static_cast<float>((copies.at(0) + 0.5 * copies.at(1)) * 0.6));
            r = m_svf.at(1).lowPass(static_cast<float>((copies.at(1) + 0.5 * copies.at(0)) * 0.6));
            break;
        }
        case Engine::Fm: {
            const FmSettings& s = p.fm;
            for (std::size_t k = s.ops.size(); k-- > 0;) {
                const FmOperator& op = s.ops.at(k);
                const double env = m_env.at(k).tick();
                m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
                if (op.level <= 0.0) {
                    m_opOut.at(k) = 0.0;
                    continue;
                }
                double mod = 0.0;
                for (std::size_t j = k + 1; j < s.ops.size(); ++j) {
                    if (s.ops.at(j).into == static_cast<int>(k) + 1) mod += m_opOut.at(j);
                }
                const double fb = op.feedback * 0.5 * (m_opOut.at(k) + m_opPrev.at(k)) * 0.3;
                const double level = op.into == 0 ? op.level : op.level * m_ctl.at(0);
                const double out = sine(m_phase.at(k) + mod + fb) * env * level;
                m_opPrev.at(k) = m_opOut.at(k);
                m_opOut.at(k) = out;
                if (op.into == 0) {
                    l += out * (1.0 - op.pan);
                    r += out * (1.0 + op.pan);
                }
            }
            l *= 0.4;
            r *= 0.4;
            break;
        }
        case Engine::Supersaw: {
            const SupersawSettings& s = p.supersaw;
            const double centre = -0.55366 * s.mix + 0.99785;
            const double sides = -0.73764 * s.mix * s.mix + 1.2841 * s.mix + 0.044372;
            double sumL = 0.0;
            double sumR = 0.0;
            for (std::size_t k = 0; k < 7; ++k) {
                m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
                const double x = saw(m_phase.at(k), m_step.at(k)) * (k == 3 ? centre : sides);
                const double place = (static_cast<double>(k) - 3.0) / 3.0 * 0.8;
                sumL += x * (1.0 - place);
                sumR += x * (1.0 + place);
            }
            const double hl = m_svf.at(0).highPass(static_cast<float>(sumL * 0.15));
            const double hr = m_svf.at(1).highPass(static_cast<float>(sumR * 0.15));
            l = m_ladder.at(0).tick(static_cast<float>(hl), m_coeffs.at(0), 1.1F);
            r = m_ladder.at(1).tick(static_cast<float>(hr), m_coeffs.at(0), 1.1F);
            break;
        }
        case Engine::Flute: {
            const FluteSettings& s = p.flute;
            Line& bore = m_lines.at(0);
            Line& jet = m_lines.at(1);
            const double boreOut = bore.read(m_ctl.at(0));
            const double pole = poleAt(0.7, c.rate);
            m_state.at(0) = (1.0 - pole) * boreOut + pole * m_state.at(0); // (Cook's loss filter, its gain -1)
            double temp = -m_state.at(0);
            // DC blocked.
            const double dc = temp - m_state.at(1) + 0.9995 * m_state.at(2);
            m_state.at(1) = temp;
            m_state.at(2) = dc;
            temp = dc;
            const double breath = m_ctl.at(2) * (1.0 + m_ctl.at(3));
            const double noise = s.breath * random.bipolar() * breath;
            jet.push(static_cast<float>(breath + noise - 0.5 * temp));
            const double fromJet = jet.read(m_ctl.at(1));
            const double into = jetTable(fromJet) + 0.5 * temp;
            bore.push(static_cast<float>(into));
            const double y = 0.3 * boreOut;
            const double air = 0.04 * noise;
            l = y + air;
            r = y - air;
            break;
        }
        case Engine::Modal: {
            const double rub = bandPass(m_svf.at(0), random.bipolar()) * m_ctl.at(0);
            const double strike = m_ctl.at(1) * (i == 0 && m_countdown == kControl ? 1.0 : 0.0) * 40.0;
            m_ctl.at(1) = 0.0;
            const double x = rub + strike;
            for (std::size_t m = 0; m < m_modeC1.size(); ++m) {
                if (m_modeGain.at(m) == 0.0) continue;
                const double y = m_modeC1.at(m) * m_modeY1.at(m) + m_modeC2.at(m) * m_modeY2.at(m) + x * m_modeGain.at(m);
                m_modeY2.at(m) = m_modeY1.at(m);
                m_modeY1.at(m) = y;
                const double side = m % 2 == 0 ? 0.4 : -0.4;
                l += y * (1.0 + side);
                r += y * (1.0 - side);
            }
            l *= 0.35;
            r *= 0.35;
            break;
        }
        case Engine::Granular: {
            for (Grain& g : m_grains) {
                if (!g.on) continue;
                const double w = g.windowSin;
                const double v = g.toneSin * w * w * g.gain;
                l += v * g.left;
                r += v * g.right;
                const double toneSin = (g.toneSin * g.toneStepCos) + (g.toneCos * g.toneStepSin);
                g.toneCos = (g.toneCos * g.toneStepCos) - (g.toneSin * g.toneStepSin);
                g.toneSin = toneSin;
                const double windowSin = (g.windowSin * g.windowStepCos) + (g.windowCos * g.windowStepSin);
                g.windowCos = (g.windowCos * g.windowStepCos) - (g.windowSin * g.windowStepSin);
                g.windowSin = windowSin;
                g.at += 1.0;
                if (g.at >= g.length) g.on = false;
            }
            l = m_svf.at(0).lowPass(static_cast<float>(l * 0.5));
            r = m_svf.at(1).lowPass(static_cast<float>(r * 0.5));
            break;
        }
        case Engine::Wind: {
            const PlaceSettings& s = p.place;
            const double nl = random.bipolar();
            const double nr = random.bipolar();
            const double gl = m_ctl.at(0);
            const double gr = m_ctl.at(1);
            const double gustL = m_svf.at(3).lowPass(static_cast<float>(nl)) * (0.15 + 0.85 * gl) * s.place;
            const double gustR = m_svf.at(4).lowPass(static_cast<float>(nr)) * (0.15 + 0.85 * gr) * s.place;
            const double mid = 0.5 * (nl + nr);
            const double howl = (bandPass(m_svf.at(0), mid) + 0.6 * bandPass(m_svf.at(1), mid) + 0.35 * bandPass(m_svf.at(2), mid)) * 10.0 * s.tone
                                * (0.3 + 0.35 * (gl + gr));
            l = gustL + howl;
            r = gustR + howl;
            break;
        }
        case Engine::Ocean: {
            const PlaceSettings& s = p.place;
            const double nl = random.bipolar();
            const double nr = random.bipolar();
            const double ampL = m_ctl.at(7);
            const double ampR = m_ctl.at(8);
            const double waveL = m_svf.at(0).lowPass(static_cast<float>(nl)) * (0.08 + 0.92 * ampL) * s.place * 1.2;
            const double waveR = m_svf.at(1).lowPass(static_cast<float>(nr)) * (0.08 + 0.92 * ampR) * s.place * 1.2;
            const double swell = 0.5 * (ampL + ampR);
            const double sing = bandPass(m_svf.at(2), 0.5 * (nl + nr)) * 12.0 * s.tone * (0.3 + 0.7 * swell);
            for (std::size_t k = 0; k < 3; ++k) m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
            const double bed = (sine(m_phase.at(0)) + 0.35 * sine(m_phase.at(1)) + 0.25 * sine(m_phase.at(2))) * 0.25 * s.tone * (0.6 + 0.4 * swell);
            l = waveL + sing + bed;
            r = waveR + sing + bed;
            break;
        }
        case Engine::Forest: {
            const PlaceSettings& s = p.place;
            const double rustle = (0.5 + 0.5 * m_ctl.at(13)) * 0.35 * s.place;
            l = m_svf.at(0).highPass(static_cast<float>(random.bipolar())) * (0.1 + 0.9 * m_ctl.at(0)) * rustle;
            r = m_svf.at(1).highPass(static_cast<float>(random.bipolar())) * (0.1 + 0.9 * m_ctl.at(1)) * rustle;
            m_phase.at(0) = wrap(m_phase.at(0) + m_step.at(0));
            m_phase.at(1) = wrap(m_phase.at(1) + m_step.at(1));
            const double bed = (sine(m_phase.at(0)) + 0.25 * sine(3.0 * m_phase.at(0)) + 0.2 * sine(m_phase.at(1))) * 0.3 * s.tone;
            l += bed;
            r += bed;
            break;
        }
        case Engine::Cave: {
            const PlaceSettings& s = p.place;
            const double wander = 0.5 + 0.5 * m_ctl.at(0);
            l = m_svf.at(0).lowPass(static_cast<float>(random.bipolar())) * 4.0 * wander * s.place;
            r = m_svf.at(1).lowPass(static_cast<float>(random.bipolar())) * 4.0 * wander * s.place;
            for (std::size_t k = 0; k < 3; ++k) m_phase.at(k) = wrap(m_phase.at(k) + m_step.at(k));
            const double drone = ((sine(m_phase.at(0)) + sine(m_phase.at(1))) * 0.2 + sine(m_phase.at(2)) * 0.08) * s.tone;
            l += drone;
            r += drone;
            break;
        }
        case Engine::Sampled: {
            if (c.samples == nullptr || m_zone >= c.samples->zones.size()) break;
            const SampleZone& z = c.samples->zones.at(m_zone);
            // The recording at `pos` (between samples; a loop's seam read across), and `pos` moved on.
            const auto read = [&z](double& pos, double step, float& a, float& b) {
                const std::size_t frames = z.left.size();
                if (z.loopEnd == 0 && pos >= static_cast<double>(frames) - 1.0) { // (rung out)
                    a = b = 0.0F;
                    return;
                }
                const auto at = static_cast<std::size_t>(pos);
                const auto f = static_cast<float>(pos - static_cast<double>(at));
                std::size_t next = at + 1;
                if (z.loopEnd > 0 && next >= z.loopEnd) next = z.loopStart;
                a = z.left.at(at) + (z.left.at(next) - z.left.at(at)) * f;
                b = z.right.at(at) + (z.right.at(next) - z.right.at(at)) * f;
                pos += step;
                if (z.loopEnd > 0 && pos >= static_cast<double>(z.loopEnd)) pos -= static_cast<double>(z.loopEnd - z.loopStart);
            };
            float a = 0.0F;
            float b = 0.0F;
            read(m_samplePos, m_sampleStep, a, b);
            if (m_state.at(1) > 0.0) {
                // Struck again: the old sound fading out over 10 ms under the new (no click).
                float oa = 0.0F;
                float ob = 0.0F;
                read(m_state.at(0), m_sampleStep, oa, ob);
                const auto g = static_cast<float>(m_state.at(1));
                a += oa * g;
                b += ob * g;
                m_state.at(1) = std::max(0.0, m_state.at(1) - m_dt / 0.01);
            }
            l = m_svf.at(0).lowPass(a) * m_ctl.at(0);
            r = m_svf.at(1).lowPass(b) * m_ctl.at(0);
            break;
        }
        case Engine::Classic: break;
        }
        *toLeft++ = static_cast<float>(l);
        *toRight++ = static_cast<float>(r);
        m_time += m_dt;
    }
}

void Rotary::prepare(double sampleRate)
{
    m_rate = sampleRate;
    m_cross.set(800.0, 0.7071, sampleRate);
    m_hornLine.allocate(static_cast<std::size_t>(0.01 * sampleRate) + 8);
    m_drumLine.allocate(static_cast<std::size_t>(0.01 * sampleRate) + 8);
}

void Rotary::tick(float& left, float& right) noexcept
{
    if (m_mix <= 0.0F) return;
    const float mono = 0.5F * (left + right);
    float low = 0.0F;
    float high = 0.0F;
    m_cross.tick(mono, low, high);
    // The horn (slow speed: about 0.8 turns a second) and the drum (a little slower).
    m_horn = wrap(m_horn + 0.83 / m_rate);
    m_drum = wrap(m_drum + 0.69 / m_rate);
    const double hs = sine(m_horn);
    const double ds = sine(m_drum);
    m_hornLine.push(high);
    m_drumLine.push(low);
    const double horn = m_hornLine.read(static_cast<float>((0.0012 + 0.0004 * hs) * m_rate)) * (1.0 + 0.3 * hs);
    const double drum = m_drumLine.read(static_cast<float>((0.0012 + 0.0002 * ds) * m_rate)) * (1.0 + 0.12 * ds);
    const double hc = sine(m_horn + 0.25);
    const double wetL = horn * (0.6 + 0.4 * hc) + drum * (0.9 + 0.1 * ds);
    const double wetR = horn * (0.6 - 0.4 * hc) + drum * (0.9 - 0.1 * ds);
    left = static_cast<float>(left * (1.0F - m_mix) + wetL * m_mix * 1.4);
    right = static_cast<float>(right * (1.0F - m_mix) + wetR * m_mix * 1.4);
}

} // namespace gigchain::engine::pads
