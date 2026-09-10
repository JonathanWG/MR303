#pragma once

#include <array>

#include "sp303/dsp/Biquad.h"
#include "sp303/dsp/Oversampler.h"
#include "sp303/dsp/Saturator.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Filter + Drive - the flagship effect and the one users reach for first.
//
//   CTRL 1 = CUTOFF     CTRL 2 = RESONANCE     CTRL 3 = DRIVE
//
// Reference implementation for every other effect in the rack. Note what it
// does structurally:
//   * cutoff is mapped exponentially, because that is how pitch is heard
//   * the non-linearity runs inside an oversampled block
//   * resonance is capped below self-oscillation until Phase 0 says otherwise
//
// STATUS: structurally correct, NOT yet matched to hardware. Filter order,
// response type, resonance law and drive curve are all placeholders pending
// measurement. Do not ship this as "emulation" until the null-test passes.
// ---------------------------------------------------------------------------
class FilterDrive final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::FilterDrive; }
    const char* ctrl1Name() const noexcept override { return "CUTOFF"; }
    const char* ctrl2Name() const noexcept override { return "RESONANCE"; }
    const char* ctrl3Name() const noexcept override { return "DRIVE"; }

private:
    static constexpr double kMinCutoffHz = 40.0;
    static constexpr double kMaxCutoffHz = 18000.0;
    static constexpr int    kOversample  = 4;

    void updateCoefficients() noexcept;

    std::array<dsp::Biquad, 2>      filters_ {};      // L, R
    std::array<dsp::Oversampler, 2> oversamplers_ {};
    dsp::Saturator                  saturator_ {};

    double sampleRate_ {44100.0};
    float  cutoff_     {1.0f};
    float  resonance_  {0.0f};
    float  drive_      {0.0f};
    bool   dirty_      {true};
};

}  // namespace sp303::fx
