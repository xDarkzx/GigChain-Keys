#pragma once

#include "SawStackDsp.h"
#include "SawStackFx.h"
#include "SawStackTypes.h"

#include <array>
#include <span>

namespace gigchain::engine::sawstack {

// The effects every one of the app's own instruments (Saw Stack Pro, Piano
// Pro) finishes its sound with, in the order producers use: level, the main
// filter, drive, EQ, chorus, OTT, width, tape, echo, reverb, then the
// sidechain over all of it and a soft ceiling. What the type fixes comes
// with setDesign(), what the knobs set with setKnobs(). Audio thread:
// process() never allocates, locks or logs.
class FxChain
{
public:
    struct Knobs
    {
        float masterGain = 1.0F;
        double cutoffHz = 20000.0, q = 0.7071; // the main 24 dB/octave low-pass
        double reverb = 0.0, delay = 0.0, ott = 0.0, width = 0.0, tape = 0.0;
        double sidechain = 0.0; // how far it ducks (0: no ducking)
        int curve = 0;          // sidechain::kCurves
        double beats = 1.0, slope = 0.5;
        bool fromNotes = false;
    };

    // Main thread, before process().
    void prepare(double sampleRate);
    // A new type's effects (drive, EQ, chorus, room, echo, loudness).
    void setDesign(const FxDesign& fx);
    void setKnobs(const Knobs& knobs);
    // The level and filter jump to the knobs (after prepare: no glide in).
    void snap();
    // A note was played at `ppq` (the sidechain may restart there).
    void noteAt(double ppq) { m_sidechain.noteAt(ppq); }
    // `left`/`right` in place; `subL`/`subR` (as long, or empty) are added
    // after the effects (a sub kept out of them). `ppq` moves on with the samples.
    void process(std::span<float> left, std::span<float> right, std::span<const float> subL, std::span<const float> subR, double tempo,
                 double& ppq, double ppqStep, Random& random) noexcept;
    [[nodiscard]] double sidechainPosition() const { return m_sidechain.position(); }

private:
    double m_rate = 48000.0;
    Knobs m_knobs{};
    float m_masterGain = 0.0F;
    float m_filterNow = 20000.0F;
    int m_filterCountdown = 0;
    std::array<Svf, 4> m_mainFilter{}; // two in a row each side: 24 dB/octave
    float m_drive = 0.0F;              // soft clip amount (0: off)
    bool m_cutMud = false;
    double m_delayFeedback = 0.3, m_delayBeats = 0.75;
    std::array<EqBand, 3> m_eq{};
    Chorus m_chorus;
    Ott m_ott;
    Dimension m_dimension;
    Tape m_tape;
    PingPong m_delay;
    Reverb m_reverb;
    Sidechain m_sidechain;
    float m_trim = 1.0F;
};

} // namespace gigchain::engine::sawstack
