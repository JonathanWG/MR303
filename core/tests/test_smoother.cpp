#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "sp303/rt/ParameterSmoother.h"

using namespace sp303::rt;
using Catch::Approx;

TEST_CASE("Smoother ramps to target over the configured time", "[rt][smoother]") {
    constexpr double sr     = 48000.0;
    constexpr double rampMs = 20.0;
    const int expectedSteps = static_cast<int>(sr * rampMs / 1000.0);

    ParameterSmoother s;
    s.prepare(sr, rampMs);
    s.reset(0.0f);
    s.setTarget(1.0f);

    CHECK(s.isSmoothing());

    for (int i = 0; i < expectedSteps; ++i) s.next();

    CHECK_FALSE(s.isSmoothing());
    CHECK(s.current() == Approx(1.0f));
}

TEST_CASE("Smoother output is monotonic with no overshoot", "[rt][smoother]") {
    // Overshoot on a filter cutoff is audible as a click at the end of a sweep.
    ParameterSmoother s;
    s.prepare(48000.0, 10.0);
    s.reset(0.0f);
    s.setTarget(1.0f);

    float previous = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        const float v = s.next();
        CHECK(v >= previous);
        CHECK(v <= 1.0f + 1e-5f);
        previous = v;
    }
}

TEST_CASE("Retargeting mid-ramp does not jump", "[rt][smoother]") {
    // Performing a knob means constantly changing the target. Each change must
    // continue from the current value, not restart from the old target.
    ParameterSmoother s;
    s.prepare(48000.0, 20.0);
    s.reset(0.0f);

    s.setTarget(1.0f);
    for (int i = 0; i < 100; ++i) s.next();

    const float mid = s.current();
    CHECK(mid > 0.0f);
    CHECK(mid < 1.0f);

    s.setTarget(0.0f);
    const float afterRetarget = s.next();

    CHECK(std::abs(afterRetarget - mid) < 0.01f);
}

TEST_CASE("Reset jumps immediately without ramping", "[rt][smoother]") {
    ParameterSmoother s;
    s.prepare(48000.0, 20.0);
    s.reset(0.75f);

    CHECK_FALSE(s.isSmoothing());
    CHECK(s.current() == Approx(0.75f));
    CHECK(s.next()    == Approx(0.75f));
}

TEST_CASE("Setting the same target twice is a no-op", "[rt][smoother]") {
    ParameterSmoother s;
    s.prepare(48000.0, 20.0);
    s.reset(0.5f);
    s.setTarget(0.5f);

    CHECK_FALSE(s.isSmoothing());
}
