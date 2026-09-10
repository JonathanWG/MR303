#pragma once

#include <cstdint>

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Amplitude quantisation (bit-depth reduction).
//
// UNVERIFIED, and it matters more than it looks:
//   - Truncation biases toward zero and produces correlated distortion that is
//     audible on fades and quiet tails.
//   - Rounding is symmetric and cleaner.
//   - Dither decorrelates the error into a benign noise floor.
//
// Public sources cite "20-bit converters" but converter resolution does NOT
// determine the internal data path width, and none of them say which rounding
// behaviour the DSP uses. Phase 0 settles it by recording a slow fade to
// silence and inspecting the residual - each mode has a distinct signature.
// ---------------------------------------------------------------------------
enum class DitherMode : std::uint8_t {
    None,      // truncate
    Round,     // nearest
    Tpdf       // triangular PDF dither
};

class Quantizer {
public:
    // bits in [1, 32]. 0 or >= 32 disables quantisation.
    void setBitDepth(int bits) noexcept;
    void setDitherMode(DitherMode mode) noexcept { dither_ = mode; }

    SP303_RT float process(float x) noexcept;

    int        bitDepth()   const noexcept { return bits_; }
    DitherMode ditherMode() const noexcept { return dither_; }

private:
    SP303_RT float nextTpdf() noexcept;

    int        bits_    {16};
    float      step_    {0.0f};
    float      invStep_ {0.0f};
    DitherMode dither_  {DitherMode::Round};

    // xorshift32 - deterministic, allocation-free, good enough for dither.
    // Deterministic matters: the golden-file regression tests must be stable.
    std::uint32_t rngState_ {0x9E3779B9u};
};

}  // namespace sp303::dsp
