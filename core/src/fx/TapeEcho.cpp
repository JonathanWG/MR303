#include "sp303/fx/TapeEcho.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

void TapeEcho::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Room for the longest delay plus the modulation excursion.
    const double longest = kMaxDelayMs * (1.0 + kWowDepth + kFlutterDepth + 0.01);
    const auto maxSamples = static_cast<int>(std::ceil(longest * 0.001 * sampleRate_));

    // Head EQ clamped under Nyquist so a low test rate keeps them filters.
    const double lowpassHz  = std::min(kHeadLowpassHz,  0.45 * sampleRate_);
    const double highpassHz = std::min(kHeadHighpassHz, 0.05 * sampleRate_);

    for (auto& channel : channels_) {
        channel.line.prepare(maxSamples);
        channel.lowpass.setLowpass(lowpassHz, 0.707, sampleRate_);
        channel.highpass.setHighpass(highpassHz, 0.707, sampleRate_);
        channel.oversampler.prepare(kOversample, maxBlockSize);
    }

    wow_.prepare(sampleRate_);
    wow_.setRate(kWowHz);
    flutter_.prepare(sampleRate_);
    flutter_.setRate(kFlutterHz);

    glideRate_ = 1.0 - std::exp(-1.0 / (kGlideSeconds * sampleRate_));

    targetSamples_  = kMinDelayMs * 0.001 * sampleRate_;
    currentSamples_ = targetSamples_;

    reset();
}

void TapeEcho::reset() noexcept {
    for (auto& channel : channels_) {
        channel.line.reset();
        channel.lowpass.reset();
        channel.highpass.reset();
        channel.oversampler.reset();
    }
    wow_.reset();
    flutter_.reset();

    // Snap rather than glide: a preset load must not sweep.
    currentSamples_ = targetSamples_;
}

void TapeEcho::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    // Exponential, as in Delay: repeat time is heard as a ratio.
    const double ms = kMinDelayMs * std::pow(kMaxDelayMs / kMinDelayMs,
                                             static_cast<double>(c1));
    targetSamples_ = ms * 0.001 * sampleRate_;

    feedback_ = c2 * kMaxFeedback;
    level_    = c3;
}

void TapeEcho::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    for (int n = 0; n < numFrames; ++n) {
        currentSamples_ += (targetSamples_ - currentSamples_) * glideRate_;

        wow_.advance();
        flutter_.advance();

        // Two incommensurate rates, so the wobble does not read as an LFO.
        const double transport = 1.0 + wow_.sine() * kWowDepth
                                     + flutter_.sine(0.37) * kFlutterDepth;
        const double delay = std::max(1.0, currentSamples_ * transport);

        for (std::size_t ch = 0; ch < 2; ++ch) {
            float* data    = (ch == 0) ? left : right;
            auto&  channel = channels_[ch];

            const float dry  = data[n];
            const float echo = channel.line.read(delay - 1.0);

            // Head bandwidth, inside the loop.
            const float fed = channel.highpass.process(channel.lowpass.process(echo));

            // Record head: what goes onto the tape is saturated, oversampled.
            const float onTape = channel.oversampler.processSample(
                dry + fed * feedback_,
                [](float v) noexcept { return tape(v); });

            channel.line.write(onTape);

            data[n] = dry + echo * level_;
        }
    }
}

}  // namespace sp303::fx
