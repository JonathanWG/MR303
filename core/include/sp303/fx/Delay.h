#pragma once

#include <array>

#include "sp303/dsp/Biquad.h"
#include "sp303/dsp/DelayLine.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Delay - direct-access button 3.
//
//   CTRL 1 = TIME     CTRL 2 = FEEDBACK     CTRL 3 = LEVEL
//
// Structurally follows FilterDrive: exponential mapping where perception is
// logarithmic, and every allocation in prepare().
//
// The delay time is glided toward its target rather than jumped to. That is
// deliberate and audible: sweeping TIME on a repeating echo bends the pitch of
// the tail, which is the single most-performed gesture on this kind of delay.
// Snapping instead would click on every knob movement.
//
// STATUS: structurally plausible, NOT matched to hardware. Time range, feedback
// law, the damping in the loop and whether the hardware's repeats degrade at
// all are placeholders pending Phase 0 measurement. UNVERIFIED - see
// docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class Delay final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Delay; }
    const char* ctrl1Name() const noexcept override { return "TIME"; }
    const char* ctrl2Name() const noexcept override { return "FEEDBACK"; }
    const char* ctrl3Name() const noexcept override { return "LEVEL"; }

private:
    static constexpr double kMinDelayMs = 20.0;
    static constexpr double kMaxDelayMs = 1000.0;

    // Capped below 1.0. A feedback path that can reach unity builds without
    // bound and is a speaker-damaging bug, not a creative option.
    static constexpr float kMaxFeedback = 0.95f;

    // Lowpass inside the feedback loop, so successive repeats get darker
    // instead of piling up identical copies. UNVERIFIED.
    static constexpr double kDampingHz = 6000.0;

    // Seconds for the delay time to glide to a new target. Slow enough to bend
    // musically, fast enough not to feel laggy.
    static constexpr double kGlideSeconds = 0.15;

    std::array<dsp::DelayLine, 2> lines_ {};   // L, R
    std::array<dsp::Biquad, 2>    damping_ {};

    double sampleRate_    {44100.0};
    double targetSamples_ {0.0};
    double currentSamples_{0.0};
    double glideRate_     {0.0};  // per-sample one-pole coefficient

    float feedback_ {0.0f};
    float level_    {0.0f};
};

}  // namespace sp303::fx
