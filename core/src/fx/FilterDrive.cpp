#include "sp303/fx/FilterDrive.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

void FilterDrive::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (auto& os : oversamplers_)
        os.prepare(kOversample, maxBlockSize);

    saturator_.setCurve(dsp::SaturationCurve::Tanh);
    dirty_ = true;
    reset();
}

void FilterDrive::reset() noexcept {
    for (auto& f : filters_)      f.reset();
    for (auto& os : oversamplers_) os.reset();
}

void FilterDrive::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    if (c1 != cutoff_ || c2 != resonance_) dirty_ = true;

    cutoff_    = c1;
    resonance_ = c2;
    drive_     = c3;
}

void FilterDrive::updateCoefficients() noexcept {
    // Exponential cutoff mapping. A linear map spends most of the knob travel
    // in a range the ear barely resolves and makes sweeps feel wrong.
    const double frequency =
        kMinCutoffHz * std::pow(kMaxCutoffHz / kMinCutoffHz,
                                static_cast<double>(cutoff_));

    // Capped below self-oscillation. If Phase 0 shows the hardware DOES
    // self-oscillate, raise this - that behaviour would be part of the sound
    // and must be reproduced, not "fixed".
    const double q = 0.707 + static_cast<double>(resonance_) * 8.0;

    for (auto& f : filters_)
        f.setLowpass(frequency, q, sampleRate_ * kOversample);

    dirty_ = false;
}

void FilterDrive::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;
    if (dirty_) updateCoefficients();

    // Drive knob maps to 1x..~32x pre-gain.
    const float driveAmount = 1.0f + drive_ * 31.0f;

    for (int channel = 0; channel < 2; ++channel) {
        float* data = (channel == 0) ? left : right;
        auto&  filter = filters_[static_cast<std::size_t>(channel)];
        auto&  os     = oversamplers_[static_cast<std::size_t>(channel)];

        // Filter then saturate, both at the oversampled rate: the filter's
        // resonant peak is what pushes the saturator, so their order and rate
        // are audible, not cosmetic.
        const auto shaper = [&filter, this, driveAmount](float x) noexcept {
            return saturator_.process(filter.process(x), driveAmount);
        };

        for (int n = 0; n < numFrames; ++n)
            data[n] = os.processSample(data[n], shaper);
    }
}

}  // namespace sp303::fx
