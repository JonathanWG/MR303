#pragma once

#include <array>

#include "sp303/dsp/DelayLine.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Reverb - MFX.
//
//   CTRL 1 = TIME     CTRL 2 = TONE     CTRL 3 = LEVEL
//
// Schroeder/Moorer topology in the Freeverb arrangement: eight parallel
// feedback combs, each with a one-pole lowpass in its loop, into four series
// allpasses, per channel. The right channel's delays are offset by a fixed
// spread so the two tails decorrelate without a second input path.
//
// Why this and not an FDN or convolution: it is what a 2001 groove sampler's
// DSP budget buys, it is allocation-free and O(1) per sample, and its
// metallic edge at short TIME is part of how a cheap reverb sounds rather
// than a defect to hide.
//
// TIME is mapped as decay time (RT60), not as raw feedback. The feedback each
// comb needs for a given decay depends on its length, so deriving it from the
// mean comb delay keeps the knob meaning the same thing at every sample rate.
//
// STATUS: NOT matched to hardware. Topology, tunings, decay range and the
// damping law are placeholders. UNVERIFIED - see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class Reverb final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Reverb; }
    const char* ctrl1Name() const noexcept override { return "TIME"; }
    const char* ctrl2Name() const noexcept override { return "TONE"; }
    const char* ctrl3Name() const noexcept override { return "LEVEL"; }

private:
    static constexpr std::size_t kCombs     = 8;
    static constexpr std::size_t kAllpasses = 4;

    // Freeverb's tunings, in samples at 44.1 kHz. Mutually incommensurate
    // lengths spread the echo density so no single comb rings through.
    static constexpr std::array<int, kCombs> kCombTuning
        {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr std::array<int, kAllpasses> kAllpassTuning
        {556, 441, 341, 225};
    static constexpr int    kStereoSpread = 23;
    static constexpr double kTuningRate   = 44100.0;

    static constexpr double kMinDecaySeconds = 0.4;
    static constexpr double kMaxDecaySeconds = 10.0;
    static constexpr double kMaxFeedback     = 0.985;

    // Damping cutoff, exponential in TONE. 0 leaves the tail open; 1 is dark.
    static constexpr double kMaxDampHz = 16000.0;
    static constexpr double kMinDampHz = 600.0;

    // Gain structure: Freeverb's input gain into the comb bank, and a wet
    // gain that puts a typical tail around -14 dB below the dry at LEVEL 1.
    // A sustained tone sitting on a comb resonance at maximum TIME can still
    // exceed unity - that is the topology, and the tests bound it rather than
    // pretend it away.
    static constexpr float kInputGain   = 0.015f;
    static constexpr float kWetGain     = 2.0f;
    static constexpr float kAllpassGain = 0.5f;

    struct Channel {
        std::array<dsp::DelayLine, kCombs>     combs        {};
        std::array<float, kCombs>              combState    {};  // one-pole memory
        std::array<double, kCombs>             combDelay    {};
        std::array<dsp::DelayLine, kAllpasses> allpasses    {};
        std::array<double, kAllpasses>         allpassDelay {};
    };

    void updateCoefficients() noexcept;

    std::array<Channel, 2> channels_ {};

    double sampleRate_      {44100.0};
    double meanCombSeconds_ {0.03};

    float feedback_ {0.8f};
    float damp_     {0.2f};
    float level_    {0.0f};
    float time_     {0.5f};
    float tone_     {0.5f};
    bool  dirty_    {true};
};

}  // namespace sp303::fx
