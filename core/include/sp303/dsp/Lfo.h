#pragma once

#include <cmath>

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Low-frequency oscillator for the modulation effects.
//
// One phase accumulator, read as many ways as needed: advance() once per
// sample, then take sine() or triangle() at any phase offset. A stereo pair
// reads the same oscillator a quarter or half cycle apart instead of running
// two accumulators that would drift out of their intended relationship.
//
// Phase is kept in cycles, [0, 1), so offsets read as fractions of a cycle
// rather than radians.
// ---------------------------------------------------------------------------
class Lfo {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setRate(rateHz_);
    }

    SP303_RT void setRate(double hz) noexcept {
        rateHz_ = hz < 0.0 ? 0.0 : hz;
        step_   = rateHz_ / sampleRate_;
    }

    SP303_RT void reset(double phaseCycles = 0.0) noexcept {
        phase_ = phaseCycles - std::floor(phaseCycles);
    }

    SP303_RT void advance() noexcept {
        phase_ += step_;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }

    SP303_RT double phase() const noexcept { return phase_; }
    double rate() const noexcept { return rateHz_; }

    // Sine in [-1, 1] at the current phase plus an offset in cycles.
    SP303_RT double sine(double offsetCycles = 0.0) const noexcept {
        return std::sin(kTwoPi * (phase_ + offsetCycles));
    }

    // Triangle in [-1, 1]: -1 at phase 0, rising to +1 at 0.5, back to -1.
    SP303_RT double triangle(double offsetCycles = 0.0) const noexcept {
        double p = phase_ + offsetCycles;
        p -= std::floor(p);
        return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
    }

private:
    static constexpr double kTwoPi = 6.283185307179586476925;

    double sampleRate_ {44100.0};
    double rateHz_     {1.0};
    double step_       {1.0 / 44100.0};
    double phase_      {0.0};
};

}  // namespace sp303::dsp
