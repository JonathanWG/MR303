#pragma once

#include <cstddef>
#include <vector>

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Fixed-length FIR with a circular delay line.
//
// Coefficients are set off the audio thread; process() never allocates.
//
// Two uses in this project:
//   1. Anti-alias / reconstruction filtering inside Oversampler and Decimator.
//   2. Modelling the SP-303's analogue output stage from a measured impulse
//      response (cheap, and it visibly improves the null-test residual).
// ---------------------------------------------------------------------------
class Fir {
public:
    // Not real-time safe - allocates. Call from prepare(), never from process().
    void setCoefficients(const std::vector<float>& taps);

    // Windowed-sinc lowpass. `cutoffNormalised` is cutoff/sampleRate in
    // [0, 0.5]. `numTaps` should be odd for a symmetric linear-phase kernel.
    void designLowpass(double cutoffNormalised, int numTaps);

    SP303_RT void  reset() noexcept;
    SP303_RT float process(float x) noexcept;

    std::size_t numTaps() const noexcept { return taps_.size(); }

private:
    std::vector<float> taps_;
    std::vector<float> state_;
    std::size_t        writeIndex_ {0};
};

}  // namespace sp303::dsp
