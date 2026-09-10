#include "sp303/fx/Delay.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

void Delay::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto maxSamples =
        static_cast<int>(std::ceil(kMaxDelayMs * 0.001 * sampleRate_));

    for (auto& line : lines_)
        line.prepare(maxSamples);

    for (auto& d : damping_)
        d.setLowpass(kDampingHz, 0.707, sampleRate_);

    // One-pole glide: reaches ~63% of a new target in kGlideSeconds.
    glideRate_ = 1.0 - std::exp(-1.0 / (kGlideSeconds * sampleRate_));

    targetSamples_  = kMinDelayMs * 0.001 * sampleRate_;
    currentSamples_ = targetSamples_;

    reset();
}

void Delay::reset() noexcept {
    for (auto& line : lines_) line.reset();
    for (auto& d : damping_) d.reset();

    // Jump to the target rather than gliding from wherever the last patch left
    // the read head - a preset load must not sweep.
    currentSamples_ = targetSamples_;
}

void Delay::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    // Exponential, for the same reason FilterDrive's cutoff is: the ear hears
    // delay time as a ratio, so a linear knob spends most of its travel in the
    // long, barely-distinguishable end.
    const double ms = kMinDelayMs * std::pow(kMaxDelayMs / kMinDelayMs,
                                             static_cast<double>(c1));
    targetSamples_ = ms * 0.001 * sampleRate_;

    feedback_ = c2 * kMaxFeedback;
    level_    = c3;
}

void Delay::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    for (int n = 0; n < numFrames; ++n) {
        // Glide once per frame, shared by both channels so they stay coherent.
        currentSamples_ += (targetSamples_ - currentSamples_) * glideRate_;

        for (int channel = 0; channel < 2; ++channel) {
            float* data = (channel == 0) ? left : right;
            auto&  line = lines_[static_cast<std::size_t>(channel)];
            auto&  damp = damping_[static_cast<std::size_t>(channel)];

            const float dry     = data[n];
            const float delayed = line.read(currentSamples_);

            // Damping sits inside the loop, not on the output: it is what makes
            // each repeat darker than the last rather than dulling the first
            // one and leaving the tail unchanged.
            line.write(dry + damp.process(delayed) * feedback_);

            data[n] = dry + delayed * level_;
        }
    }
}

}  // namespace sp303::fx
