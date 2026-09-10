#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "sp303/dsp/Biquad.h"

using namespace sp303::dsp;
using Catch::Approx;

namespace {

// RMS of a steady sine at `frequency` after passing through `filter`.
// The first half is discarded so the filter's transient does not skew the
// measurement.
double measureResponse(Biquad& filter, double frequency, double sampleRate,
                       int numSamples = 8192) {
    filter.reset();

    double sumSquares = 0.0;
    const int settle = numSamples / 2;

    for (int i = 0; i < numSamples; ++i) {
        const auto phase = 2.0 * 3.14159265358979 * frequency
                         * static_cast<double>(i) / sampleRate;
        const float y = filter.process(static_cast<float>(std::sin(phase)));
        if (i >= settle) sumSquares += static_cast<double>(y) * y;
    }
    return std::sqrt(sumSquares / (numSamples - settle));
}

}  // namespace

TEST_CASE("Lowpass passes below cutoff and rejects above", "[dsp][biquad]") {
    constexpr double sr = 48000.0;
    Biquad filter;
    filter.setLowpass(1000.0, 0.707, sr);

    const double passband = measureResponse(filter, 100.0,   sr);
    const double stopband = measureResponse(filter, 10000.0, sr);

    CHECK(passband > 0.6);          // roughly unity RMS for a sine (~0.707)
    CHECK(stopband < 0.05);
    CHECK(stopband < passband);
}

TEST_CASE("Highpass mirrors the lowpass behaviour", "[dsp][biquad]") {
    constexpr double sr = 48000.0;
    Biquad filter;
    filter.setHighpass(1000.0, 0.707, sr);

    CHECK(measureResponse(filter, 100.0,  sr) < 0.05);
    CHECK(measureResponse(filter, 10000.0, sr) > 0.6);
}

TEST_CASE("Resonance lifts the response at cutoff", "[dsp][biquad]") {
    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;

    Biquad flat, resonant;
    flat.setLowpass(fc, 0.707, sr);
    resonant.setLowpass(fc, 8.0, sr);

    CHECK(measureResponse(resonant, fc, sr) > measureResponse(flat, fc, sr));
}

TEST_CASE("Filter stays stable when cutoff is pushed past Nyquist", "[dsp][biquad]") {
    // A swept filter that reaches Nyquist blows up. clampFrequency() exists to
    // prevent that; this test is the guard on it.
    constexpr double sr = 48000.0;
    Biquad filter;
    filter.setLowpass(sr, 20.0, sr);   // absurd request, must be clamped

    float x = 1.0f;
    for (int i = 0; i < 10000; ++i) {
        const float y = filter.process(x);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::abs(y) < 100.0f);
        x = 0.0f;
    }
}

TEST_CASE("Reset clears filter state", "[dsp][biquad]") {
    constexpr double sr = 48000.0;
    Biquad filter;
    filter.setLowpass(500.0, 2.0, sr);

    for (int i = 0; i < 100; ++i) filter.process(1.0f);
    filter.reset();

    // With cleared state, the first output is just b0 * x.
    const float first = filter.process(0.0f);
    CHECK(first == Approx(0.0f).margin(1e-6));
}
