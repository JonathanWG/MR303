#pragma once

#include <atomic>
#include <cmath>

#include "sp303/rt/RtSafe.h"

namespace sp303::rt {

// ---------------------------------------------------------------------------
// Linear ramp for a continuous parameter.
//
// The CTRL 1/2/3 knobs are the expressive core of the instrument - they get
// swept hard during performance and during resampling. Applying a raw target
// value per block produces stepped discontinuities ("zipper noise"), which is
// an artefact of our implementation, not of the hardware, so it must not be
// audible.
//
// Usage on the audio thread:
//     smoother.setTarget(atomicParam.load(std::memory_order_relaxed));
//     for (int i = 0; i < n; ++i)
//         out[i] = process(in[i], smoother.next());
// ---------------------------------------------------------------------------
class ParameterSmoother {
public:
    void prepare(double sampleRate, double rampMs = 20.0) noexcept {
        const auto steps = (sampleRate * rampMs) / 1000.0;
        stepsTotal_ = steps > 1.0 ? static_cast<int>(steps) : 1;
        stepsLeft_  = 0;
        increment_  = 0.0f;
    }

    // Jump immediately, no ramp. For preset load / reset, never mid-performance.
    SP303_RT void reset(float value) noexcept {
        current_   = value;
        target_    = value;
        stepsLeft_ = 0;
        increment_ = 0.0f;
    }

    SP303_RT void setTarget(float value) noexcept {
        if (value == target_) return;
        target_    = value;
        increment_ = (target_ - current_) / static_cast<float>(stepsTotal_);
        stepsLeft_ = stepsTotal_;
    }

    SP303_RT float next() noexcept {
        if (stepsLeft_ <= 0) return current_;
        current_ += increment_;
        if (--stepsLeft_ == 0) current_ = target_;
        return current_;
    }

    SP303_RT float current() const noexcept { return current_; }
    SP303_RT bool  isSmoothing() const noexcept { return stepsLeft_ > 0; }

private:
    float current_    {0.0f};
    float target_     {0.0f};
    float increment_  {0.0f};
    int   stepsLeft_  {0};
    int   stepsTotal_ {1};
};

}  // namespace sp303::rt
