#include "sp303/fx/Pitch.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Hann window over a normalised position. Two of these, half a period apart,
// sum to exactly 1 - which is what makes the crossfade between the two read
// heads gain-neutral instead of pumping at the window rate.
inline float hann(double normalisedPosition) noexcept {
    return static_cast<float>(0.5 * (1.0 - std::cos(2.0 * kPi * normalisedPosition)));
}

}  // namespace

void Pitch::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Two heads live inside one window, so the line needs a full window plus
    // the half-window offset, with room for the interpolation partner.
    const auto maxSamples =
        static_cast<int>(std::ceil(kMaxWindowMs * 0.001 * sampleRate_)) * 2 + 4;

    for (auto& line : lines_)
        line.prepare(maxSamples);

    windowFrames_ = kMinWindowMs * 0.001 * sampleRate_;
    reset();
}

void Pitch::reset() noexcept {
    for (auto& line : lines_) line.reset();
    readOffset_ = 0.0;
}

void Pitch::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    // Centre detent: 0.5 is exactly unison, so the knob has a musically
    // meaningful home position rather than one that is merely close.
    const double semitones = (static_cast<double>(c1) - 0.5) * 2.0 * kSemitoneRange;
    ratio_ = std::pow(2.0, semitones / 12.0);

    // Window length is exponential too - the audible difference between 12 and
    // 20 ms is far larger than between 100 and 120.
    windowFrames_ = kMinWindowMs
                  * std::pow(kMaxWindowMs / kMinWindowMs, static_cast<double>(c2))
                  * 0.001 * sampleRate_;

    // Keep the head inside the new window when GRAIN is turned down, otherwise
    // it reads stale audio until it happens to wrap.
    if (readOffset_ >= windowFrames_)
        readOffset_ = std::fmod(readOffset_, windowFrames_);

    mix_ = c3;
}

void Pitch::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;
    if (windowFrames_ <= 1.0) return;

    // Read position moves at `ratio_`, so the offset behind the write head
    // grows at (1 - ratio). Shifting up makes the offset shrink and wrap the
    // other way, which is why it is wrapped in both directions below.
    const double offsetStep = 1.0 - ratio_;
    const double half       = windowFrames_ * 0.5;

    for (int n = 0; n < numFrames; ++n) {
        readOffset_ += offsetStep;
        if (readOffset_ >= windowFrames_) readOffset_ -= windowFrames_;
        else if (readOffset_ < 0.0)       readOffset_ += windowFrames_;

        const double offsetB = (readOffset_ < half) ? readOffset_ + half
                                                    : readOffset_ - half;

        const float gainA = hann(readOffset_ / windowFrames_);
        const float gainB = hann(offsetB     / windowFrames_);

        for (int channel = 0; channel < 2; ++channel) {
            float* data = (channel == 0) ? left : right;
            auto&  line = lines_[static_cast<std::size_t>(channel)];

            const float dry = data[n];
            line.write(dry);

            const float wet = line.read(readOffset_) * gainA
                            + line.read(offsetB)     * gainB;

            data[n] = dry + (wet - dry) * mix_;
        }
    }
}

}  // namespace sp303::fx
