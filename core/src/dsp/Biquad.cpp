#include "sp303/dsp/Biquad.h"

#include <algorithm>
#include <cmath>

namespace sp303::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Keep the cutoff below Nyquist with a safety margin. A swept filter that is
// allowed to reach Nyquist goes unstable and produces a very loud, very
// unpleasant blowup - the kind of bug that damages speakers and ears.
inline double clampFrequency(double f, double sampleRate) noexcept {
    return std::clamp(f, 10.0, sampleRate * 0.49);
}
}  // namespace

void Biquad::reset() noexcept {
    z1_ = 0.0f;
    z2_ = 0.0f;
}

void Biquad::setCoefficients(double b0, double b1, double b2,
                             double a0, double a1, double a2) noexcept {
    const double inv = 1.0 / a0;
    b0_ = static_cast<float>(b0 * inv);
    b1_ = static_cast<float>(b1 * inv);
    b2_ = static_cast<float>(b2 * inv);
    a1_ = static_cast<float>(a1 * inv);
    a2_ = static_cast<float>(a2 * inv);
}

void Biquad::setLowpass(double frequency, double q, double sampleRate) noexcept {
    const double f     = clampFrequency(frequency, sampleRate);
    const double w0    = 2.0 * kPi * f / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(q, 0.01));

    setCoefficients((1.0 - cosw0) * 0.5,
                     1.0 - cosw0,
                    (1.0 - cosw0) * 0.5,
                     1.0 + alpha,
                    -2.0 * cosw0,
                     1.0 - alpha);
}

void Biquad::setHighpass(double frequency, double q, double sampleRate) noexcept {
    const double f     = clampFrequency(frequency, sampleRate);
    const double w0    = 2.0 * kPi * f / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(q, 0.01));

    setCoefficients( (1.0 + cosw0) * 0.5,
                    -(1.0 + cosw0),
                     (1.0 + cosw0) * 0.5,
                      1.0 + alpha,
                     -2.0 * cosw0,
                      1.0 - alpha);
}

void Biquad::setBandpass(double frequency, double q, double sampleRate) noexcept {
    const double f     = clampFrequency(frequency, sampleRate);
    const double w0    = 2.0 * kPi * f / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(q, 0.01));

    setCoefficients( alpha,
                     0.0,
                    -alpha,
                     1.0 + alpha,
                    -2.0 * cosw0,
                     1.0 - alpha);
}

void Biquad::setPeaking(double frequency, double q, double gainDb,
                        double sampleRate) noexcept {
    const double f     = clampFrequency(frequency, sampleRate);
    const double A     = std::pow(10.0, gainDb / 40.0);
    const double w0    = 2.0 * kPi * f / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(q, 0.01));

    setCoefficients( 1.0 + alpha * A,
                    -2.0 * cosw0,
                     1.0 - alpha * A,
                     1.0 + alpha / A,
                    -2.0 * cosw0,
                     1.0 - alpha / A);
}

}  // namespace sp303::dsp
