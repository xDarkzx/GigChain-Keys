#include "FxChain.h"

namespace gigchain::engine::sawstack {

namespace {

constexpr float kSmoothing = 0.002F; // per sample: about 10 ms at 48 kHz
constexpr int kFilterUpdate = 16;    // samples between filter cutoff updates

} // namespace

void FxChain::prepare(double sampleRate)
{
    m_rate = sampleRate;
    m_chorus.prepare(m_rate);
    m_ott.prepare(m_rate);
    m_dimension.prepare(m_rate);
    m_tape.prepare(m_rate);
    m_delay.prepare(m_rate);
    m_reverb.prepare(m_rate);
    m_eq.at(0).set(EqBand::Kind::Peak, 500.0, -2.5, 1.0, m_rate);
    m_eq.at(1).set(EqBand::Kind::Peak, 3500.0, -2.0, 1.2, m_rate);
    m_eq.at(2).set(EqBand::Kind::HighShelf, 10000.0, 2.0, 0.7071, m_rate);
}

void FxChain::setDesign(const FxDesign& fx)
{
    m_drive = fx.driveDb <= 0.0 ? 0.0F : static_cast<float>(std::pow(10.0, fx.driveDb / 20.0));
    m_cutMud = fx.cutMud;
    m_chorus.setMix(fx.chorus);
    Reverb::Settings room;
    room.decaySeconds = fx.reverbDecay;
    room.size = fx.reverbSize;
    m_reverb.set(room);
    m_trim = static_cast<float>(std::pow(10.0, fx.trimDb / 20.0));
    m_delayFeedback = fx.delayFeedback;
    m_delayBeats = fx.delayBeats;
    m_delay.set(m_knobs.delay, m_delayFeedback, m_delayBeats);
}

void FxChain::setKnobs(const Knobs& knobs)
{
    m_knobs = knobs;
    m_reverb.setMix(knobs.reverb);
    m_delay.set(knobs.delay, m_delayFeedback, m_delayBeats);
    m_ott.setMix(knobs.ott);
    m_dimension.setAmount(knobs.width);
    m_tape.setAmount(knobs.tape);
    m_sidechain.set(knobs.sidechain, knobs.curve, knobs.beats, knobs.slope, knobs.fromNotes);
}

void FxChain::snap()
{
    m_masterGain = m_knobs.masterGain;
    m_filterNow = static_cast<float>(m_knobs.cutoffHz);
}

void FxChain::process(std::span<float> left, std::span<float> right, std::span<const float> subL, std::span<const float> subR,
                      double tempo, double& ppq, double ppqStep, Random& random) noexcept
{
    const bool withSub = subL.size() >= left.size() && subR.size() >= left.size();
    auto toR = right.begin();
    auto fromSubL = subL.begin();
    auto fromSubR = subR.begin();
    for (float& outL : left) {
        float& outR = *toR++;
        m_masterGain += (m_knobs.masterGain - m_masterGain) * kSmoothing;
        float l = outL * m_masterGain;
        float r = outR * m_masterGain;
        const float bassL = withSub ? *fromSubL++ * m_masterGain : 0.0F;
        const float bassR = withSub ? *fromSubR++ * m_masterGain : 0.0F;
        // The main filter (24 dB/octave): glides to the knob, never steps.
        m_filterNow += (static_cast<float>(m_knobs.cutoffHz) - m_filterNow) * 0.002F;
        if (m_filterCountdown-- <= 0) {
            m_filterCountdown = kFilterUpdate;
            for (Svf& f : m_mainFilter) f.set(m_filterNow, m_knobs.q, m_rate);
        }
        if (m_filterNow < 19500.0F || m_knobs.q > 0.71) {
            l = m_mainFilter.at(1).lowPass(m_mainFilter.at(0).lowPass(l));
            r = m_mainFilter.at(3).lowPass(m_mainFilter.at(2).lowPass(r));
        }
        if (m_drive > 0.0F) {
            l = std::tanh(l * m_drive) / m_drive;
            r = std::tanh(r * m_drive) / m_drive;
        }
        if (m_cutMud) {
            for (EqBand& band : m_eq) {
                l = band.tick(l, 0);
                r = band.tick(r, 1);
            }
        }
        m_chorus.tick(l, r);
        m_ott.tick(l, r);
        m_dimension.tick(l, r);
        m_tape.tick(l, r, random);
        m_delay.tick(l, r, tempo);
        m_reverb.tick(l, r);
        const float duck = m_sidechain.gain(ppq) * m_trim;
        ppq += ppqStep;
        l = (l + bassL) * duck;
        r = (r + bassR) * duck;
        // A soft ceiling: nothing leaves past full scale, the peaks rounded off.
        const auto ceiling = [](float x) {
            const float a = std::abs(x);
            return a <= 0.7F ? x : std::copysign(0.7F + 0.3F * std::tanh((a - 0.7F) / 0.3F), x);
        };
        outL = ceiling(l);
        outR = ceiling(r);
    }
}

} // namespace gigchain::engine::sawstack
