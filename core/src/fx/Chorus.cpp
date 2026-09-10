#include "sp303/fx/Chorus.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

void Chorus::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
    centreFrames_ = kCentreMs * 0.001 * sampleRate_;

    const auto maxSamples = static_cast<int>(
        std::ceil((kCentreMs + kMaxDepthMs + 1.0) * 0.001 * sampleRate_));

    for (auto& line : lines_) line.prepare(maxSamples);

    lfo_.prepare(sampleRate_);
    lfo_.setRate(kMinRateHz);

    reset();
}

void Chorus::reset() noexcept {
    for (auto& line : lines_) line.reset();
    lfo_.reset();
}

void Chorus::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    // Exponential: a sweep rate is heard as a ratio, like every other rate.
    lfo_.setRate(kMinRateHz * std::pow(kMaxRateHz / kMinRateHz,
                                        static_cast<double>(c1)));
    depth_ = c2;
    level_ = c3;
}

void Chorus::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    const double depthFrames =
        static_cast<double>(depth_) * kMaxDepthMs * 0.001 * sampleRate_;

    for (int n = 0; n < numFrames; ++n) {
        lfo_.advance();

        const double delay[2] {
            centreFrames_ + lfo_.sine() * depthFrames,
            centreFrames_ + lfo_.sine(kRightPhaseOffset) * depthFrames,
        };

        for (std::size_t ch = 0; ch < 2; ++ch) {
            float* data = (ch == 0) ? left : right;
            auto&  line = lines_[ch];

            // Write first, then read: a request for `d` frames is exactly `d`
            // frames of delay, and there is no feedback to need otherwise.
            line.write(data[n]);
            const float wet = line.read(std::max(0.0, delay[ch]));

            data[n] = data[n] + wet * level_;
        }
    }
}

}  // namespace sp303::fx
