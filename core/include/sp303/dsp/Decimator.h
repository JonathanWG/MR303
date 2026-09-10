#pragma once

#include "sp303/Types.h"
#include "sp303/dsp/Fir.h"
#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Sample-rate reduction for the Long (22.05 kHz) and LoFi (11.025 kHz) modes.
//
// *** THE ANTI-ALIAS FILTER IS THE WHOLE POINT, AND IT IS UNVERIFIED. ***
//
// Two very different outcomes hang on one measurement:
//   - Strong anti-aliasing -> clean, dark, "warm"           (filterStrength ~ 1)
//   - Weak/absent          -> deliberate aliasing, metallic (filterStrength ~ 0)
//
// The SP-303's reputation for a distinctive lo-fi character suggests the
// filtering is NOT textbook-clean, but that is an inference, not a measurement.
// Phase 0 resolves it: feed a high-frequency sweep in LoFi mode and look at
// where energy lands. Aliased content mirrors around Nyquist and is unmistakable.
//
// `filterStrength` exists so the measured answer becomes a single fitted
// parameter rather than a code rewrite.
// ---------------------------------------------------------------------------
class Decimator {
public:
    // hostRate is the DAW's rate; mode picks the emulated internal rate.
    void prepare(double hostRate, QualityMode mode, int numTaps = 63);

    // 0 = no anti-aliasing (maximum aliasing), 1 = full anti-aliasing.
    void setFilterStrength(float strength) noexcept;

    SP303_RT void  reset() noexcept;
    SP303_RT float process(float x) noexcept;

    double emulatedRate() const noexcept { return emulatedRate_; }

private:
    Fir    preFilter_;
    Fir    postFilter_;
    double hostRate_      {44100.0};
    double emulatedRate_  {44100.0};
    double phase_         {0.0};
    double increment_     {1.0};
    float  held_          {0.0f};
    float  strength_      {1.0f};
};

}  // namespace sp303::dsp
