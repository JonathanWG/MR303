#include "sp303/dsp/Saturator.h"

#include <algorithm>
#include <cmath>

namespace sp303::dsp {

float Saturator::tanhCurve(float x) noexcept {
    return std::tanh(x);
}

float Saturator::hardClip(float x) noexcept {
    return std::clamp(x, -1.0f, 1.0f);
}

float Saturator::cubicSoft(float x) noexcept {
    if (x <= -1.0f) return -2.0f / 3.0f;
    if (x >=  1.0f) return  2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}

float Saturator::asymmetric(float x) noexcept {
    // Different curvature per half-cycle introduces even harmonics.
    return x >= 0.0f ? std::tanh(x)
                     : std::tanh(x * 0.7f) * 0.85f;
}

float Saturator::process(float x, float drive) const noexcept {
    const float d = std::max(drive, 0.0001f);
    const float driven = x * d;

    float y = 0.0f;
    switch (curve_) {
        case SaturationCurve::Tanh:       y = tanhCurve(driven);  break;
        case SaturationCurve::HardClip:   y = hardClip(driven);   break;
        case SaturationCurve::CubicSoft:  y = cubicSoft(driven);  break;
        case SaturationCurve::Asymmetric: y = asymmetric(driven); break;
    }

    // Gain compensation: keeps perceived level roughly constant across the
    // DRIVE sweep. Square-root rather than 1/d so the level still lifts a
    // little as you push, which is what players expect from a drive control.
    return y / std::sqrt(d);
}

}  // namespace sp303::dsp
