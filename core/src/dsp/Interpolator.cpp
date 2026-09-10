#include "sp303/dsp/Interpolator.h"

#include <cmath>

namespace sp303::dsp {

namespace {

// Reads with zero outside [0, numFrames). Keeps the 4-point kernel from
// reaching past either edge of the buffer.
inline float at(const float* data, int numFrames, int index) noexcept {
    return (index >= 0 && index < numFrames) ? data[index] : 0.0f;
}

}  // namespace

float Interpolator::dropSample(const float* data, int numFrames, double pos) noexcept {
    const auto i = static_cast<int>(std::floor(pos));
    return at(data, numFrames, i);
}

float Interpolator::linear(const float* data, int numFrames, double pos) noexcept {
    const auto  i = static_cast<int>(std::floor(pos));
    const auto  f = static_cast<float>(pos - static_cast<double>(i));
    const float a = at(data, numFrames, i);
    const float b = at(data, numFrames, i + 1);
    return a + f * (b - a);
}

float Interpolator::cubic(const float* data, int numFrames, double pos) noexcept {
    // Catmull-Rom, 4-point / 3rd-order.
    const auto  i = static_cast<int>(std::floor(pos));
    const auto  f = static_cast<float>(pos - static_cast<double>(i));

    const float p0 = at(data, numFrames, i - 1);
    const float p1 = at(data, numFrames, i);
    const float p2 = at(data, numFrames, i + 1);
    const float p3 = at(data, numFrames, i + 2);

    const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float b =         p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float c = -0.5f * p0             + 0.5f * p2;

    return ((a * f + b) * f + c) * f + p1;
}

float Interpolator::read(InterpolationMode mode, const float* data,
                         int numFrames, double pos) noexcept {
    switch (mode) {
        case InterpolationMode::DropSample: return dropSample(data, numFrames, pos);
        case InterpolationMode::Linear:     return linear(data, numFrames, pos);
        case InterpolationMode::Cubic:      return cubic(data, numFrames, pos);
    }
    return 0.0f;
}

}  // namespace sp303::dsp
