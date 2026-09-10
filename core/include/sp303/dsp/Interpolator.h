#pragma once

#include <cstdint>

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Fractional-position sample readout.
//
// *** THIS IS THE SINGLE BIGGEST DETERMINANT OF THE INSTRUMENT'S CHARACTER. ***
//
// Whenever a sample plays back at anything other than its native rate (pitch,
// varispeed, time-stretch), the interpolation algorithm decides what the
// artefacts sound like:
//
//   DropSample (zero-order hold) - aggressive imaging/aliasing, "gritty",
//                                 classic cheap-sampler signature
//   Linear                      - characteristic high-end rolloff that gets
//                                 worse as you transpose down
//   Cubic (Catmull-Rom)         - clean; usually TOO clean to be authentic
//
// Which one the SP-303 actually uses is UNVERIFIED. Public documentation does
// not say. Phase 0 settles it by playing a pure tone at a matrix of pitch
// values and measuring where the aliasing products land - each algorithm has a
// distinct fingerprint.
//
// All three are implemented so the measurement can be A/B'd against real
// hardware captures rather than guessed at.
// ---------------------------------------------------------------------------
enum class InterpolationMode : std::uint8_t {
    DropSample,
    Linear,
    Cubic
};

class Interpolator {
public:
    // `pos` is a fractional index into `data`. Out-of-range reads return 0
    // rather than clamping: a voice that has run past its END point must go
    // silent, not hold the last sample forever.
    SP303_RT static float dropSample(const float* data, int numFrames, double pos) noexcept;
    SP303_RT static float linear    (const float* data, int numFrames, double pos) noexcept;
    SP303_RT static float cubic     (const float* data, int numFrames, double pos) noexcept;

    SP303_RT static float read(InterpolationMode mode, const float* data,
                               int numFrames, double pos) noexcept;
};

}  // namespace sp303::dsp
