#pragma once

#include <vector>

#include "sp303/dsp/Fir.h"
#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Integer-factor oversampling wrapper for non-linear stages.
//
// Wrap ANY waveshaper in this. Without it, the harmonics a saturator generates
// above Nyquist fold back down as inharmonic aliasing - an artefact of our
// implementation that the hardware does not have.
//
// PERFORMANCE NOTE: this is the straightforward zero-stuff + FIR formulation.
// It is correct and easy to verify, but it does N times the filter work for an
// N-times oversampler. Before shipping, replace with a polyphase halfband
// decomposition (roughly 4x cheaper for the same stopband). Tracked as a known
// optimisation, not a correctness issue - the interface stays the same.
// ---------------------------------------------------------------------------
class Oversampler {
public:
    // factor must be >= 1. Allocates - call from prepare(), not process().
    void prepare(int factor, int maxBlockSize, int numTaps = 63);

    SP303_RT void reset() noexcept;

    // Runs `shaper` at the oversampled rate, once per oversampled frame.
    //
    // TEMPLATED ON PURPOSE - do NOT change this to std::function.
    //
    // Constructing a std::function from a lambda whose captures exceed the
    // small-buffer size ALLOCATES. Two pointers plus a float already overflows
    // it on most implementations, so the "convenient" signature would put a
    // heap allocation in the inner loop of the audio thread. A template keeps
    // the call direct and inlinable, with no indirection and no allocation.
    //
    // `shaper` must itself be real-time safe.
    template <typename Shaper>
    SP303_RT float processSample(float x, Shaper&& shaper) noexcept {
        if (factor_ <= 1)
            return shaper(x);

        float result = 0.0f;

        for (int i = 0; i < factor_; ++i) {
            // Zero-stuff: only the first phase carries the input sample. The
            // gain of `factor_` compensates for the energy the zeros remove.
            const float stuffed = (i == 0) ? x * static_cast<float>(factor_) : 0.0f;

            const float up     = upFilter_.process(stuffed);
            const float shaped = shaper(up);
            const float down   = downFilter_.process(shaped);

            // Decimate by keeping one phase.
            if (i == 0) result = down;
        }

        return result;
    }

    int factor() const noexcept { return factor_; }

private:
    Fir                upFilter_;
    Fir                downFilter_;
    int                factor_ {1};
    std::vector<float> scratch_;
};

}  // namespace sp303::dsp
