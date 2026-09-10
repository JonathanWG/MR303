#include "sp303/dsp/Fir.h"

#include <algorithm>
#include <cmath>

namespace sp303::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void Fir::setCoefficients(const std::vector<float>& taps) {
    taps_  = taps;
    state_.assign(taps_.size(), 0.0f);
    writeIndex_ = 0;
}

void Fir::designLowpass(double cutoffNormalised, int numTaps) {
    if (numTaps % 2 == 0) ++numTaps;  // force odd -> symmetric, linear phase
    numTaps = std::max(numTaps, 3);

    const double fc     = std::clamp(cutoffNormalised, 1.0e-4, 0.4999);
    const int    middle = numTaps / 2;

    std::vector<float> taps(static_cast<std::size_t>(numTaps));
    double sum = 0.0;

    for (int n = 0; n < numTaps; ++n) {
        const int    m = n - middle;
        const double sinc = (m == 0) ? 2.0 * fc
                                     : std::sin(2.0 * kPi * fc * m) / (kPi * m);

        // Blackman window - ~-74 dB stopband, enough that the anti-alias
        // filter is not itself the thing colouring the sound.
        const double t = static_cast<double>(n) / static_cast<double>(numTaps - 1);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * t)
                              + 0.08 * std::cos(4.0 * kPi * t);

        const double v = sinc * w;
        taps[static_cast<std::size_t>(n)] = static_cast<float>(v);
        sum += v;
    }

    // Normalise to unity DC gain.
    if (sum != 0.0) {
        const auto inv = static_cast<float>(1.0 / sum);
        for (auto& t : taps) t *= inv;
    }

    setCoefficients(taps);
}

void Fir::reset() noexcept {
    std::fill(state_.begin(), state_.end(), 0.0f);
    writeIndex_ = 0;
}

float Fir::process(float x) noexcept {
    const auto n = taps_.size();
    if (n == 0) return x;

    state_[writeIndex_] = x;

    float acc = 0.0f;
    std::size_t idx = writeIndex_;
    for (std::size_t i = 0; i < n; ++i) {
        acc += taps_[i] * state_[idx];
        idx = (idx == 0) ? n - 1 : idx - 1;
    }

    writeIndex_ = (writeIndex_ + 1) % n;
    return acc;
}

}  // namespace sp303::dsp
