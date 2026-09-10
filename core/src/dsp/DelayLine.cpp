#include "sp303/dsp/DelayLine.h"

#include <algorithm>
#include <cmath>

namespace sp303::dsp {

void DelayLine::prepare(int maxDelaySamples) {
    // +2 so a read at the maximum delay still has an interpolation partner,
    // and so a zero-length request does not produce an empty buffer.
    const auto size = static_cast<std::size_t>(std::max(maxDelaySamples, 1) + 2);
    data_.assign(size, 0.0f);
    write_ = 0;
}

void DelayLine::reset() noexcept {
    std::fill(data_.begin(), data_.end(), 0.0f);
    write_ = 0;
}

float DelayLine::read(double delaySamples) const noexcept {
    const auto size = data_.size();
    if (size == 0) return 0.0f;

    // The most recent sample sits one behind the write head.
    const double maxDelay = static_cast<double>(size) - 2.0;
    const double d = std::clamp(delaySamples, 0.0, std::max(maxDelay, 0.0));

    const auto   whole    = static_cast<std::size_t>(d);
    const auto   fraction = static_cast<float>(d - static_cast<double>(whole));

    // write_ points at the NEXT slot to be written, so "0 samples ago" is
    // write_ - 1. Adding `size` keeps the unsigned arithmetic positive.
    const std::size_t base = write_ + size - 1;
    const std::size_t i0   = (base - whole) % size;
    const std::size_t i1   = (i0 == 0) ? size - 1 : i0 - 1;

    const float a = data_[i0];
    const float b = data_[i1];
    return a + fraction * (b - a);
}

}  // namespace sp303::dsp
