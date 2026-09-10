#include "sp303/dsp/Quantizer.h"

#include <algorithm>
#include <cmath>

namespace sp303::dsp {

void Quantizer::setBitDepth(int bits) noexcept {
    bits_ = std::clamp(bits, 1, 32);
    if (bits_ >= 32) {
        step_    = 0.0f;
        invStep_ = 0.0f;
        return;
    }
    const auto levels = static_cast<float>(1u << (bits_ - 1));
    step_    = 1.0f / levels;
    invStep_ = levels;
}

float Quantizer::nextTpdf() noexcept {
    // Two independent uniform draws summed -> triangular distribution.
    auto next = [this]() noexcept {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return static_cast<float>(rngState_) / static_cast<float>(0xFFFFFFFFu);
    };
    return (next() + next()) - 1.0f;  // [-1, 1], triangular
}

float Quantizer::process(float x) noexcept {
    if (step_ <= 0.0f) return x;

    float v = x;
    if (dither_ == DitherMode::Tpdf)
        v += nextTpdf() * step_ * 0.5f;

    const float scaled = v * invStep_;
    const float q = (dither_ == DitherMode::None) ? std::trunc(scaled)
                                                  : std::round(scaled);
    return std::clamp(q * step_, -1.0f, 1.0f);
}

}  // namespace sp303::dsp
