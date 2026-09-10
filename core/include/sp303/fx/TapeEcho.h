#pragma once

#include <array>

#include "sp303/dsp/Biquad.h"
#include "sp303/dsp/DelayLine.h"
#include "sp303/dsp/Lfo.h"
#include "sp303/dsp/Oversampler.h"
#include "sp303/dsp/Saturator.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Tape Echo - MFX.
//
//   CTRL 1 = RATE     CTRL 2 = INTENSITY     CTRL 3 = LEVEL
//
// The same delay line as Delay, with the three things that make a tape loop
// sound like one and a digital delay not:
//
//   * the record head saturates. The waveshaper sits INSIDE the loop, so
//     INTENSITY is allowed past unity: the repeats run away into a
//     self-sustaining, compressed echo and the tape bounds them, not a cap on
//     the knob. It runs oversampled, or its harmonics would alias.
//   * the heads have a bandwidth. A lowpass and a highpass in the loop take a
//     little top and bottom off every pass, so repeats recede instead of
//     piling up identical copies.
//   * the transport is not steady. Slow wow and faster flutter modulate the
//     read head, and RATE glides like a motor rather than jumping.
//
// STATUS: NOT matched to hardware. Time range, head bandwidth, drive, wow and
// flutter figures are placeholders. UNVERIFIED - see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class TapeEcho final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::TapeEcho; }
    const char* ctrl1Name() const noexcept override { return "RATE"; }
    const char* ctrl2Name() const noexcept override { return "INTENSITY"; }
    const char* ctrl3Name() const noexcept override { return "LEVEL"; }

private:
    static constexpr double kMinDelayMs = 60.0;
    static constexpr double kMaxDelayMs = 700.0;

    // Past unity on purpose - see the class comment. The waveshaper's ceiling
    // is what holds the runaway at a level instead of at infinity.
    static constexpr float kMaxFeedback = 1.05f;

    // Drive into the tanh. Sets how hard the repeats compress and, with the
    // feedback, where a runaway settles.
    static constexpr float kTapeDrive  = 1.5f;
    static constexpr int   kOversample = 2;

    // Record/playback head bandwidth, applied once per pass around the loop.
    static constexpr double kHeadLowpassHz  = 4500.0;
    static constexpr double kHeadHighpassHz = 90.0;

    // Motor inertia: a RATE change bends the pitch of the repeats on the way.
    static constexpr double kGlideSeconds = 0.3;

    // Transport instability, as fractions of the delay time.
    static constexpr double kWowHz        = 0.8;
    static constexpr double kWowDepth     = 0.0025;
    static constexpr double kFlutterHz    = 6.5;
    static constexpr double kFlutterDepth = 0.0006;

    struct Channel {
        dsp::DelayLine   line;
        dsp::Biquad      lowpass;
        dsp::Biquad      highpass;
        dsp::Oversampler oversampler;
    };

    // The tape. Unity gain for small signals, so INTENSITY means what it says
    // until the loop actually saturates.
    SP303_RT static float tape(float x) noexcept {
        return dsp::Saturator::tanhCurve(x * kTapeDrive) / kTapeDrive;
    }

    std::array<Channel, 2> channels_ {};
    dsp::Lfo wow_;
    dsp::Lfo flutter_;

    double sampleRate_     {44100.0};
    double targetSamples_  {0.0};
    double currentSamples_ {0.0};
    double glideRate_      {0.0};

    float feedback_ {0.0f};
    float level_    {0.0f};
};

}  // namespace sp303::fx
