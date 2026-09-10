#pragma once

#include <array>

#include "sp303/dsp/Biquad.h"
#include "sp303/dsp/Lfo.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Phaser - MFX.
//
//   CTRL 1 = RATE     CTRL 2 = DEPTH     CTRL 3 = RES
//
// Four first-order allpass stages in series, their corner frequency swept
// together by a triangle, mixed equally with the dry signal. Where the cascade
// reaches 180 degrees the sum cancels: two notches that slide up and down the
// spectrum. RES feeds the cascade's output back into its input, which sharpens
// the peaks between the notches.
//
// The sweep is in octaves about a centre, so it covers the same musical range
// at every position; DEPTH 0 parks the notches. The feedback path carries a
// DC blocker: an allpass passes DC at +1, so without it RES would be a bass
// boost rather than a resonance. The right channel sweeps a quarter cycle on.
//
// STATUS: NOT matched to hardware. Stage count, centre, sweep width and the
// feedback cap are placeholders. UNVERIFIED - see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class Phaser final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Phaser; }
    const char* ctrl1Name() const noexcept override { return "RATE"; }
    const char* ctrl2Name() const noexcept override { return "DEPTH"; }
    const char* ctrl3Name() const noexcept override { return "RES"; }

private:
    static constexpr std::size_t kStages = 4;

    static constexpr double kMinRateHz      = 0.05;
    static constexpr double kMaxRateHz      = 4.0;
    static constexpr double kCentreHz       = 800.0;
    static constexpr double kMaxSweepOctaves = 2.5;
    static constexpr double kMinCornerHz    = 20.0;

    // Positive feedback around an allpass chain peaks hard at the frequencies
    // it passes in phase; this cap keeps the peaks musical rather than
    // squealing. Bounded in the tests.
    static constexpr float  kMaxFeedback       = 0.6f;
    static constexpr double kFeedbackHighpassHz = 40.0;

    static constexpr double kRightPhaseOffset = 0.25;

    struct Stage {
        float x1 {0.0f};
        float y1 {0.0f};
    };

    struct Channel {
        std::array<Stage, kStages> stages {};
        dsp::Biquad dcBlock;
        float last {0.0f};  // cascade output of the previous frame
    };

    std::array<Channel, 2> channels_ {};
    dsp::Lfo lfo_;

    double sampleRate_ {44100.0};
    float  depth_    {0.0f};
    float  feedback_ {0.0f};
};

}  // namespace sp303::fx
