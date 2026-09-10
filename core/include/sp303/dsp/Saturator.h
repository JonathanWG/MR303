#pragma once

#include <cstdint>

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Waveshaping curves for the DRIVE stage of Filter+Drive.
//
// *** MUST BE RUN INSIDE AN OVERSAMPLED BLOCK. ***
//
// Any static non-linearity generates harmonics above Nyquist. At 1x those fold
// back down as inharmonic aliasing that the hardware does not have, so the
// emulation would sound WORSE than the original in a way users describe as
// "harsh" or "digital". See Oversampler.h.
//
// Which curve the SP-303 uses is UNVERIFIED. Phase 0 identifies it by driving
// sine waves at increasing amplitude through the hardware and fitting the
// measured harmonic series - see tools/identify/fit_saturation.py.
// ---------------------------------------------------------------------------
enum class SaturationCurve : std::uint8_t {
    Tanh,       // smooth, symmetric, odd harmonics
    HardClip,   // abrupt, very bright
    CubicSoft,  // gentle knee then hard limit
    Asymmetric  // adds even harmonics - "tube-like"
};

class Saturator {
public:
    void setCurve(SaturationCurve curve) noexcept { curve_ = curve; }

    // `drive` is a linear pre-gain (1.0 = unity). Output is gain-compensated
    // so sweeping DRIVE changes timbre without a large jump in loudness.
    SP303_RT float process(float x, float drive) const noexcept;

    SP303_RT static float tanhCurve(float x) noexcept;
    SP303_RT static float hardClip(float x) noexcept;
    SP303_RT static float cubicSoft(float x) noexcept;
    SP303_RT static float asymmetric(float x) noexcept;

private:
    SaturationCurve curve_ {SaturationCurve::Tanh};
};

}  // namespace sp303::dsp
