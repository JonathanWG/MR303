#pragma once

#include "sp303/Types.h"
#include "sp303/rt/RtSafe.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Interface every one of the 26 effects implements.
//
// The three CTRL knobs are normalised [0, 1] and mapped to musically useful
// ranges inside each effect. That mapping is per-effect and is a Phase 0
// deliverable: the hardware's taper is part of how it feels to perform, and a
// linear taper on a filter cutoff feels wrong even when the endpoints match.
//
// Design constraint carried over from the hardware: exactly ONE effect is
// active at a time in Authentic mode. That is not a limitation to work around
// - it is what forces the resample-to-stack workflow that defines the
// instrument. See EffectRack.
// ---------------------------------------------------------------------------
class IEffect {
public:
    virtual ~IEffect() = default;

    // Allocation is allowed here. Never in process().
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    SP303_RT virtual void reset() noexcept = 0;

    // ctrl1/2/3 are normalised [0, 1], already smoothed by the caller.
    SP303_RT virtual void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept = 0;

    // In-place stereo processing.
    SP303_RT virtual void process(float* left, float* right, int numFrames) noexcept = 0;

    virtual EffectId id() const noexcept = 0;

    // Labels shown in the GUI context bar - the 3-digit display alone cannot
    // tell the user what the knobs currently do.
    virtual const char* ctrl1Name() const noexcept = 0;
    virtual const char* ctrl2Name() const noexcept = 0;
    virtual const char* ctrl3Name() const noexcept = 0;
};

}  // namespace sp303::fx
