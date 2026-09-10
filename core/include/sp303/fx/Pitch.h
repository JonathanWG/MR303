#pragma once

#include <array>

#include "sp303/dsp/DelayLine.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Pitch - direct-access button 2.
//
//   CTRL 1 = PITCH     CTRL 2 = GRAIN     CTRL 3 = MIX
//
// Two-tap crossfading delay line ("rotating tap") pitch shifter. A read head
// moving through the delay line at a rate other than 1.0 reads the signal back
// transposed; a second head half a window ahead, with complementary Hann
// gains, covers the discontinuity when the first one wraps.
//
// Consequence worth knowing before turning MIX up at unison: with PITCH
// centred the heads stop moving and the wet path is a fixed half-window
// delay, so mixing it against dry combs. That is inherent to the topology,
// not a bug - at unison there is nothing to shift, and the useful settings
// are either PITCH away from centre or MIX at 1.
//
// Why this and not a phase vocoder: this is the algorithm the hardware
// generation this instrument comes from could actually afford, it is
// allocation-free and O(1) per sample, and its artefacts - the periodic
// warble at the window rate - are part of how a cheap pitch shifter sounds
// rather than a defect to hide. GRAIN exposes the window length precisely
// because that trade-off is a performance control.
//
// STATUS: NOT matched to hardware. Whether the SP-303's PITCH is a shifter at
// all or a varispeed of the whole sample, its range, and its window behaviour
// are UNVERIFIED - see docs/HARDWARE_FACTS.md. Do not describe this as an
// emulation until Phase 0 has run.
// ---------------------------------------------------------------------------
class Pitch final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Pitch; }
    const char* ctrl1Name() const noexcept override { return "PITCH"; }
    const char* ctrl2Name() const noexcept override { return "GRAIN"; }
    const char* ctrl3Name() const noexcept override { return "MIX"; }

private:
    static constexpr double kSemitoneRange = 12.0;   // +/- one octave
    static constexpr double kMinWindowMs   = 12.0;
    static constexpr double kMaxWindowMs   = 120.0;

    std::array<dsp::DelayLine, 2> lines_ {};  // L, R

    double sampleRate_  {44100.0};
    double windowFrames_{0.0};

    // Read-head offset, in frames behind the write head. Advances by
    // (1 - ratio) per frame and wraps inside [0, windowFrames_).
    double readOffset_  {0.0};

    double ratio_ {1.0};
    float  mix_   {0.0f};
};

}  // namespace sp303::fx
