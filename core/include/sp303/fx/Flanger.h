#pragma once

#include <array>

#include "sp303/dsp/DelayLine.h"
#include "sp303/dsp/Lfo.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Flanger - MFX.
//
//   CTRL 1 = RATE     CTRL 2 = DEPTH     CTRL 3 = RES
//
// A very short delay, swept by a triangle and fed back on itself, mixed
// equally with the dry signal. Equal mix is what lets the comb's notches reach
// all the way down; RES sharpens the peaks between them. The right channel
// sweeps in the opposite direction.
//
// DEPTH scales the excursion about the middle of the range, so at DEPTH 0 the
// comb stands still and RATE does nothing - a fixed comb is a sound in its
// own right. There is no dry-only setting: selecting the flanger means
// flanging.
//
// STATUS: NOT matched to hardware. Delay range, LFO shape and feedback cap are
// placeholders. UNVERIFIED - see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class Flanger final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Flanger; }
    const char* ctrl1Name() const noexcept override { return "RATE"; }
    const char* ctrl2Name() const noexcept override { return "DEPTH"; }
    const char* ctrl3Name() const noexcept override { return "RES"; }

private:
    static constexpr double kMinRateHz  = 0.05;
    static constexpr double kMaxRateHz  = 4.0;
    static constexpr double kMinDelayMs = 0.5;
    static constexpr double kMaxDelayMs = 6.0;

    // Below unity by a margin: a comb at the edge of oscillation is a jet
    // engine, and past it is a bug.
    static constexpr float kMaxFeedback = 0.8f;

    static constexpr double kRightPhaseOffset = 0.5;

    std::array<dsp::DelayLine, 2> lines_ {};
    dsp::Lfo lfo_;

    double sampleRate_ {44100.0};
    float  depth_    {0.0f};
    float  feedback_ {0.0f};
};

}  // namespace sp303::fx
