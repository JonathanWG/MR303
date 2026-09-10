#pragma once

#include <cstdint>

#include "sp303/rt/RtSafe.h"

namespace sp303::seq {

// ---------------------------------------------------------------------------
// Timing correction for recorded pattern performances.
//
// The musically important part is SWING, not the grid snap. Straight
// quantisation to a grid is trivial and sounds mechanical; the whole boom-bap
// idiom lives in how far the off-beats are pushed late.
//
// Convention used here: swing delays every ODD subdivision by a percentage of
// the grid interval.
//   50%  = no swing (perfectly even)
//   67%  = triplet feel, the common boom-bap setting
//   >75% = extreme, rarely musical
//
// UNVERIFIED: the SP-303's exact swing law and available grid values.
// tools/measure/ has a routine that records a known performance and reports
// where each event actually landed, which recovers the real curve.
// ---------------------------------------------------------------------------
enum class Grid : std::uint8_t {
    Off,
    Quarter,        // 1/4
    Eighth,         // 1/8
    EighthTriplet,  // 1/8T
    Sixteenth,      // 1/16
    SixteenthTriplet,
    ThirtySecond    // 1/32
};

// Grid interval in quarter notes. Returns 0 for Grid::Off.
constexpr double gridInQuarterNotes(Grid g) noexcept {
    switch (g) {
        case Grid::Off:              return 0.0;
        case Grid::Quarter:          return 1.0;
        case Grid::Eighth:           return 0.5;
        case Grid::EighthTriplet:    return 1.0 / 3.0;
        case Grid::Sixteenth:        return 0.25;
        case Grid::SixteenthTriplet: return 1.0 / 6.0;
        case Grid::ThirtySecond:     return 0.125;
    }
    return 0.0;
}

// ---------------------------------------------------------------------------
// Snaps `positionInQuarters` to the grid with swing applied.
//
// `swingPercent` in [50, 75]; 50 is straight.
// `strength` in [0, 1] blends between the raw and the quantised position -
// full correction robs the performance of the human timing that makes it
// worth recording. Authentic mode uses 1.0 (the hardware is destructive and
// absolute); Modern mode exposes it.
// ---------------------------------------------------------------------------
SP303_RT double applyQuantize(double positionInQuarters,
                              Grid   grid,
                              double swingPercent = 50.0,
                              double strength     = 1.0) noexcept;

}  // namespace sp303::seq
