#include "sp303/fx/Flanger.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

void Flanger::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto maxSamples = static_cast<int>(
        std::ceil((kMaxDelayMs + 1.0) * 0.001 * sampleRate_));

    for (auto& line : lines_) line.prepare(maxSamples);

    lfo_.prepare(sampleRate_);
    lfo_.setRate(kMinRateHz);

    reset();
}

void Flanger::reset() noexcept {
    for (auto& line : lines_) line.reset();
    lfo_.reset();
}

void Flanger::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    lfo_.setRate(kMinRateHz * std::pow(kMaxRateHz / kMinRateHz,
                                        static_cast<double>(c1)));
    depth_    = c2;
    feedback_ = c3 * kMaxFeedback;
}

void Flanger::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    const double framesPerMs = 0.001 * sampleRate_;
    const double middleMs    = 0.5 * (kMinDelayMs + kMaxDelayMs);
    const double excursionMs = 0.5 * (kMaxDelayMs - kMinDelayMs)
                             * static_cast<double>(depth_);

    for (int n = 0; n < numFrames; ++n) {
        lfo_.advance();

        // Total loop delay per channel, in frames. Never below one frame.
        const double delay[2] {
            std::max(1.0, (middleMs + lfo_.triangle() * excursionMs) * framesPerMs),
            std::max(1.0, (middleMs + lfo_.triangle(kRightPhaseOffset) * excursionMs)
                              * framesPerMs),
        };

        for (std::size_t ch = 0; ch < 2; ++ch) {
            float* data = (ch == 0) ? left : right;
            auto&  line = lines_[ch];

            const float dry = data[n];

            // Read before write so the feedback can include this frame; the
            // -1 makes a request for `d` frames exactly `d` frames of delay.
            const float wet = line.read(delay[ch] - 1.0);
            line.write(dry + wet * feedback_);

            data[n] = 0.5f * (dry + wet);
        }
    }
}

}  // namespace sp303::fx
