#include "sp303/fx/Isolator.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

namespace {

// Knob taper: 0 is silence, 0.5 is unity, 1 is a modest lift. Linear in dB on
// each side of centre, because that is what the hand expects from a kill EQ -
// a linear-in-amplitude taper puts "half volume" at 0.5 and leaves the bottom
// half of the travel doing almost nothing audible.
float bandGain(float control, double minDb, double maxDb, float silenceBelow) noexcept {
    const float c = std::clamp(control, 0.0f, 1.0f);
    if (c <= silenceBelow) return 0.0f;

    const double db = (c < 0.5f)
        ? minDb + (static_cast<double>(c) / 0.5) * (0.0 - minDb)
        : (static_cast<double>(c) - 0.5) / 0.5 * maxDb;

    return static_cast<float>(std::pow(10.0, db / 20.0));
}

}  // namespace

void Isolator::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients();
    reset();
}

void Isolator::updateCoefficients() noexcept {
    for (auto& bands : channels_) {
        for (auto& f : bands.lowpassLow)
            f.setLowpass(kLowCrossoverHz, kButterworthQ, sampleRate_);
        for (auto& f : bands.highpassLow)
            f.setHighpass(kLowCrossoverHz, kButterworthQ, sampleRate_);
        for (auto& f : bands.lowpassHigh)
            f.setLowpass(kHighCrossoverHz, kButterworthQ, sampleRate_);
        for (auto& f : bands.highpassHigh)
            f.setHighpass(kHighCrossoverHz, kButterworthQ, sampleRate_);
    }
}

void Isolator::reset() noexcept {
    for (auto& bands : channels_) {
        for (auto& f : bands.lowpassLow)   f.reset();
        for (auto& f : bands.highpassLow)  f.reset();
        for (auto& f : bands.lowpassHigh)  f.reset();
        for (auto& f : bands.highpassHigh) f.reset();
    }
}

void Isolator::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    lowGain_  = bandGain(ctrl1, kMinBandDb, kMaxBandDb, kSilenceThreshold);
    midGain_  = bandGain(ctrl2, kMinBandDb, kMaxBandDb, kSilenceThreshold);
    highGain_ = bandGain(ctrl3, kMinBandDb, kMaxBandDb, kSilenceThreshold);
}

void Isolator::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    for (int index = 0; index < 2; ++index) {
        float* data  = (index == 0) ? left : right;
        auto&  bands = channels_[static_cast<std::size_t>(index)];

        for (int n = 0; n < numFrames; ++n) {
            const float x = data[n];

            // Low band: LP4 at the lower crossover.
            float low = x;
            for (auto& f : bands.lowpassLow) low = f.process(low);

            // Everything above it, then split again for mid and high.
            float upper = x;
            for (auto& f : bands.highpassLow) upper = f.process(upper);

            float mid = upper;
            for (auto& f : bands.lowpassHigh) mid = f.process(mid);

            float high = upper;
            for (auto& f : bands.highpassHigh) high = f.process(high);

            data[n] = low * lowGain_ + mid * midGain_ + high * highGain_;
        }
    }
}

}  // namespace sp303::fx
