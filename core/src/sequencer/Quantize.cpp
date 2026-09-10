#include "sp303/sequencer/Quantize.h"

#include <algorithm>
#include <cmath>

namespace sp303::seq {

double applyQuantize(double positionInQuarters,
                     Grid   grid,
                     double swingPercent,
                     double strength) noexcept {
    const double interval = gridInQuarterNotes(grid);
    if (interval <= 0.0) return positionInQuarters;

    const double clampedStrength = std::clamp(strength, 0.0, 1.0);
    if (clampedStrength <= 0.0) return positionInQuarters;

    // Snap to the nearest grid line.
    const double steps  = positionInQuarters / interval;
    const double index  = std::round(steps);
    double snapped      = index * interval;

    // Swing: push odd subdivisions late by a fraction of the interval.
    // At 50% the offset is zero, which is the straight case.
    const double swing = std::clamp(swingPercent, 50.0, 75.0);
    const auto   isOdd = (static_cast<long long>(index) % 2LL) != 0LL;

    if (isOdd && swing > 50.0) {
        const double offset = interval * ((swing - 50.0) / 100.0) * 2.0;
        snapped += offset;
    }

    // Partial correction preserves some of the original feel.
    return positionInQuarters + (snapped - positionInQuarters) * clampedStrength;
}

}  // namespace sp303::seq
