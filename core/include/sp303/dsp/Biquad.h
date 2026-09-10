#pragma once

#include "sp303/rt/RtSafe.h"

namespace sp303::dsp {

// ---------------------------------------------------------------------------
// Transposed Direct Form II biquad.
//
// TDF-II is chosen over DF-I for its better numerical behaviour with float
// coefficients when the cutoff is swept hard, which is exactly what happens
// when CTRL 1 is being performed.
//
// Coefficients follow the RBJ audio EQ cookbook.
//
// NOTE: the SP-303's Filter+Drive topology (pole count, response shape,
// resonance behaviour at extremes) is UNVERIFIED. This biquad is the building
// block; how many are cascaded and with what response is a Phase 0 output.
// ---------------------------------------------------------------------------
class Biquad {
public:
    void reset() noexcept;

    // frequency in Hz, q dimensionless, sampleRate in Hz.
    void setLowpass  (double frequency, double q, double sampleRate) noexcept;
    void setHighpass (double frequency, double q, double sampleRate) noexcept;
    void setBandpass (double frequency, double q, double sampleRate) noexcept;
    void setPeaking  (double frequency, double q, double gainDb, double sampleRate) noexcept;

    SP303_RT float process(float x) noexcept {
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    void setCoefficients(double b0, double b1, double b2,
                         double a0, double a1, double a2) noexcept;

    float b0_ {1.0f}, b1_ {0.0f}, b2_ {0.0f};
    float a1_ {0.0f}, a2_ {0.0f};
    float z1_ {0.0f}, z2_ {0.0f};
};

}  // namespace sp303::dsp
