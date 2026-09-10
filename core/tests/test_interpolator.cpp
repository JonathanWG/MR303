#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "sp303/dsp/Interpolator.h"

using namespace sp303::dsp;
using Catch::Approx;

TEST_CASE("Interpolator reads exact values at integer positions", "[interp]") {
    const std::array<float, 5> data {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    const int n = static_cast<int>(data.size());

    for (int i = 0; i < n; ++i) {
        const auto pos = static_cast<double>(i);
        CHECK(Interpolator::dropSample(data.data(), n, pos) == Approx(data[static_cast<std::size_t>(i)]));
        CHECK(Interpolator::linear(data.data(), n, pos)     == Approx(data[static_cast<std::size_t>(i)]));
        CHECK(Interpolator::cubic(data.data(), n, pos)      == Approx(data[static_cast<std::size_t>(i)]).margin(1e-5));
    }
}

TEST_CASE("Linear interpolation is exact on a ramp", "[interp]") {
    // A linear ramp is the one signal linear interpolation reproduces perfectly.
    std::vector<float> ramp(16);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i);

    const int n = static_cast<int>(ramp.size());
    CHECK(Interpolator::linear(ramp.data(), n, 3.25) == Approx(3.25f));
    CHECK(Interpolator::linear(ramp.data(), n, 7.5)  == Approx(7.5f));
    CHECK(Interpolator::linear(ramp.data(), n, 0.75) == Approx(0.75f));
}

TEST_CASE("Drop-sample holds the previous value across the interval", "[interp]") {
    const std::array<float, 4> data {10.0f, 20.0f, 30.0f, 40.0f};
    const int n = static_cast<int>(data.size());

    CHECK(Interpolator::dropSample(data.data(), n, 1.0)  == Approx(20.0f));
    CHECK(Interpolator::dropSample(data.data(), n, 1.4)  == Approx(20.0f));
    CHECK(Interpolator::dropSample(data.data(), n, 1.99) == Approx(20.0f));
    CHECK(Interpolator::dropSample(data.data(), n, 2.0)  == Approx(30.0f));
}

TEST_CASE("Out-of-range reads return silence, never a held sample", "[interp]") {
    // A voice past its END must go silent. Clamping instead would leave a DC
    // offset ringing until the next note.
    const std::array<float, 4> data {1.0f, 1.0f, 1.0f, 1.0f};
    const int n = static_cast<int>(data.size());

    for (auto mode : {InterpolationMode::DropSample,
                      InterpolationMode::Linear,
                      InterpolationMode::Cubic}) {
        CHECK(Interpolator::read(mode, data.data(), n, -5.0)  == Approx(0.0f));
        CHECK(Interpolator::read(mode, data.data(), n, 100.0) == Approx(0.0f));
    }
}

TEST_CASE("Cubic tracks a smooth curve more closely than linear", "[interp]") {
    // On a sine, the higher-order kernel should have lower error. This is the
    // property that makes cubic "too clean" for an authentic emulation - the
    // test documents the difference rather than asserting cubic is preferable.
    constexpr int N = 64;
    std::vector<float> sine(N);
    for (int i = 0; i < N; ++i)
        sine[static_cast<std::size_t>(i)] =
            std::sin(2.0f * 3.14159265f * static_cast<float>(i) / 16.0f);

    double linearError = 0.0;
    double cubicError  = 0.0;

    for (double pos = 8.0; pos < 40.0; pos += 0.137) {
        const double exact = std::sin(2.0 * 3.14159265 * pos / 16.0);
        linearError += std::abs(Interpolator::linear(sine.data(), N, pos) - exact);
        cubicError  += std::abs(Interpolator::cubic(sine.data(), N, pos)  - exact);
    }

    CHECK(cubicError < linearError);
}
