#pragma once

#include <array>

#include "sp303/dsp/DelayLine.h"
#include "sp303/dsp/Lfo.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Chorus - MFX.
//
//   CTRL 1 = RATE     CTRL 2 = DEPTH     CTRL 3 = LEVEL
//
// A short delay whose length is swept by a sine, mixed back against the dry
// signal. The sweep detunes the wet path; the mix is what turns detuning into
// thickness. The right channel reads the same oscillator a quarter of a cycle
// on, so the two sides detune differently at every instant and the image
// widens without a second LFO that could drift.
//
// LEVEL scales the wet path only, so 0 is an exact bypass.
//
// STATUS: NOT matched to hardware. Centre delay, depth and rate ranges are
// placeholders. UNVERIFIED - see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class Chorus final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Chorus; }
    const char* ctrl1Name() const noexcept override { return "RATE"; }
    const char* ctrl2Name() const noexcept override { return "DEPTH"; }
    const char* ctrl3Name() const noexcept override { return "LEVEL"; }

private:
    static constexpr double kMinRateHz = 0.1;
    static constexpr double kMaxRateHz = 5.0;

    // Short enough not to read as an echo, long enough to detune.
    static constexpr double kCentreMs   = 12.0;
    static constexpr double kMaxDepthMs = 5.0;

    static constexpr double kRightPhaseOffset = 0.25;

    std::array<dsp::DelayLine, 2> lines_ {};
    dsp::Lfo lfo_;

    double sampleRate_   {44100.0};
    double centreFrames_ {0.0};
    float  depth_ {0.0f};
    float  level_ {0.0f};
};

}  // namespace sp303::fx
