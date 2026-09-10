#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sp303/sequencer/Quantize.h"

using namespace sp303::seq;
using Catch::Approx;

TEST_CASE("Grid::Off leaves the position untouched", "[seq][quantize]") {
    CHECK(applyQuantize(1.234, Grid::Off) == Approx(1.234));
}

TEST_CASE("Quantize snaps to the nearest grid line", "[seq][quantize]") {
    CHECK(applyQuantize(1.05, Grid::Quarter)   == Approx(1.0));
    CHECK(applyQuantize(0.95, Grid::Quarter)   == Approx(1.0));
    CHECK(applyQuantize(0.26, Grid::Sixteenth) == Approx(0.25));
}

TEST_CASE("Straight 50% swing leaves even grid positions alone", "[seq][quantize]") {
    CHECK(applyQuantize(0.5,  Grid::Eighth, 50.0) == Approx(0.5));
    CHECK(applyQuantize(1.0,  Grid::Eighth, 50.0) == Approx(1.0));
}

TEST_CASE("Swing delays odd subdivisions only", "[seq][quantize]") {
    // The off-beats are what carry the boom-bap feel; the down-beats must
    // stay put or the whole pattern drags.
    const double evenBeat = applyQuantize(1.0, Grid::Eighth, 67.0);
    CHECK(evenBeat == Approx(1.0));

    const double oddBeat = applyQuantize(0.5, Grid::Eighth, 67.0);
    CHECK(oddBeat > 0.5);
}

TEST_CASE("More swing means more delay", "[seq][quantize]") {
    const double light = applyQuantize(0.5, Grid::Eighth, 55.0);
    const double heavy = applyQuantize(0.5, Grid::Eighth, 70.0);
    CHECK(heavy > light);
}

TEST_CASE("Strength blends between raw and quantised timing", "[seq][quantize]") {
    // Full correction robs a performance of the human timing that made it
    // worth recording. Partial strength is how Modern mode exposes that.
    constexpr double raw = 1.10;

    const double full = applyQuantize(raw, Grid::Quarter, 50.0, 1.0);
    const double none = applyQuantize(raw, Grid::Quarter, 50.0, 0.0);
    const double half = applyQuantize(raw, Grid::Quarter, 50.0, 0.5);

    CHECK(full == Approx(1.0));
    CHECK(none == Approx(raw));
    CHECK(half == Approx(1.05));
}

TEST_CASE("Swing percent is clamped to a musical range", "[seq][quantize]") {
    // Below 50 would push notes early, which is not what a swing control means.
    CHECK(applyQuantize(0.5, Grid::Eighth, 10.0) == Approx(0.5));

    const double extreme = applyQuantize(0.5, Grid::Eighth, 200.0);
    const double capped  = applyQuantize(0.5, Grid::Eighth, 75.0);
    CHECK(extreme == Approx(capped));
}

TEST_CASE("Grid intervals convert correctly to quarter notes", "[seq][quantize]") {
    CHECK(gridInQuarterNotes(Grid::Quarter)      == Approx(1.0));
    CHECK(gridInQuarterNotes(Grid::Eighth)       == Approx(0.5));
    CHECK(gridInQuarterNotes(Grid::Sixteenth)    == Approx(0.25));
    CHECK(gridInQuarterNotes(Grid::ThirtySecond) == Approx(0.125));
    CHECK(gridInQuarterNotes(Grid::EighthTriplet) == Approx(1.0 / 3.0));
    CHECK(gridInQuarterNotes(Grid::Off)          == Approx(0.0));
}
