#pragma once

#include <cstddef>
#include <vector>

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Fixed-capacity circular delay line with fractional read.
//
// Shared by Delay, Pitch and Vinyl Sim. All three need "read N.xxx samples
// ago"; writing that three times would give three chances to get the wrap-
// around arithmetic subtly wrong.
//
// The buffer is sized once in prepare(). read() clamps rather than wrapping
// past the write head: a delay longer than the line is a caller bug, and
// silently aliasing it onto fresh audio would sound like a broken feedback
// loop rather than like a mistake.
// ---------------------------------------------------------------------------
class DelayLine {
public:
    // Allocates. Call from prepare(), never from process().
    void prepare(int maxDelaySamples);

    SP303_RT void reset() noexcept;

    SP303_RT void write(float x) noexcept {
        if (data_.empty()) return;
        data_[write_] = x;
        write_ = (write_ + 1 == data_.size()) ? 0 : write_ + 1;
    }

    // `delaySamples` counts back from the most recently written sample:
    // 0 returns it, 1 the one before it. Linear interpolation between the two
    // neighbouring integer taps, which is what lets the delay time be swept
    // continuously without stepping.
    SP303_RT float read(double delaySamples) const noexcept;

    int capacity() const noexcept { return static_cast<int>(data_.size()); }

private:
    std::vector<float> data_;
    std::size_t        write_ {0};
};

}  // namespace sp303::dsp
