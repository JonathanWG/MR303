#include "sp303/dsp/Decimator.h"

#include <algorithm>

namespace sp303::dsp {

void Decimator::prepare(double hostRate, QualityMode mode, int numTaps) {
    hostRate_     = hostRate;
    emulatedRate_ = sampleRateFor(mode);

    // Never "upsample": if the host runs below the emulated rate there is
    // nothing to decimate.
    emulatedRate_ = std::min(emulatedRate_, hostRate_);
    increment_    = emulatedRate_ / hostRate_;

    const double cutoff = (emulatedRate_ * 0.5) / hostRate_;
    preFilter_.designLowpass(cutoff, numTaps);
    postFilter_.designLowpass(cutoff, numTaps);

    reset();
}

void Decimator::setFilterStrength(float strength) noexcept {
    strength_ = std::clamp(strength, 0.0f, 1.0f);
}

void Decimator::reset() noexcept {
    preFilter_.reset();
    postFilter_.reset();
    phase_ = 0.0;
    held_  = 0.0f;
}

float Decimator::process(float x) noexcept {
    if (increment_ >= 1.0) return x;  // nothing to do at Standard

    // Band-limit before dropping samples. Blending against the dry signal is
    // how `filterStrength` maps a measured amount of leakage onto one knob.
    const float filtered = preFilter_.process(x);
    const float in = filtered * strength_ + x * (1.0f - strength_);

    // Sample-and-hold at the emulated rate: this is what creates the imaging
    // artefacts that make lo-fi modes sound the way they do.
    phase_ += increment_;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
        held_ = in;
    }

    const float out = postFilter_.process(held_);
    return out * strength_ + held_ * (1.0f - strength_);
}

}  // namespace sp303::dsp
