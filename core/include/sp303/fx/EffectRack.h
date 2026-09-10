#pragma once

#include <memory>
#include <vector>

#include "sp303/Types.h"
#include "sp303/fx/IEffect.h"
#include "sp303/rt/ParameterSmoother.h"

namespace sp303::fx {

// First id behind the MFX button. Everything from here to Count is an MFX
// algorithm; everything before it has a dedicated button.
inline constexpr EffectId kFirstMfx = EffectId::Reverb;

// Display name for every id, implemented or not - the panel and the host's
// parameter list need a stable label for each slot in the enum.
constexpr const char* effectName(EffectId id) noexcept {
    switch (id) {
        case EffectId::None:             return "NO EFFECT";
        case EffectId::FilterDrive:      return "FILTER+DRIVE";
        case EffectId::Pitch:            return "PITCH";
        case EffectId::Delay:            return "DELAY";
        case EffectId::VinylSim:         return "VINYL SIM";
        case EffectId::Isolator:         return "ISOLATOR";
        case EffectId::Reverb:           return "REVERB";
        case EffectId::TapeEcho:         return "TAPE ECHO";
        case EffectId::Slicer:           return "SLICER";
        case EffectId::VoiceTransformer: return "VOICE TRANSFORMER";
        case EffectId::Distortion:       return "DISTORTION";
        case EffectId::LoFiFx:           return "LO-FI";
        case EffectId::Compressor:       return "COMPRESSOR";
        case EffectId::Chorus:           return "CHORUS";
        case EffectId::Flanger:          return "FLANGER";
        case EffectId::Phaser:           return "PHASER";
        case EffectId::Count:            break;
    }
    return "-";
}

// ---------------------------------------------------------------------------
// Holds every effect instance and routes audio through the selected one.
//
// KEY DESIGN POINT: all effects are constructed and prepared up front. Switching
// effects on the audio thread is then just moving a pointer - no allocation, no
// waiting. The memory cost of holding 26 idle effects is trivial next to the
// cost of a dropout.
//
// In Authentic mode exactly one effect is active, which is what pushes users
// into the resample-to-stack workflow. Modern mode can chain, but that is a
// deliberate departure and is gated behind FidelityMode.
// ---------------------------------------------------------------------------
class EffectRack {
public:
    EffectRack();

    void prepare(double sampleRate, int maxBlockSize);
    SP303_RT void reset() noexcept;

    // Never allocates - the instance already exists. Selecting an id with no
    // instance yet (an MFX algorithm that is not written) is an honest bypass:
    // process() passes the audio through untouched.
    SP303_RT void selectEffect(EffectId id) noexcept;
    SP303_RT EffectId currentEffectId() const noexcept { return currentId_; }

    // Whether an instance exists for this id. None has no instance and reports
    // false; it is a selection, not an effect.
    bool isAvailable(EffectId id) const noexcept;

    // Raw normalised knob positions. Smoothing happens inside process().
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept;

    SP303_RT void process(float* left, float* right, int numFrames) noexcept;

    // For the GUI context bar.
    const char* ctrl1Name() const noexcept;
    const char* ctrl2Name() const noexcept;
    const char* ctrl3Name() const noexcept;

private:
    SP303_RT IEffect* current() noexcept;

    std::vector<std::unique_ptr<IEffect>> effects_;
    EffectId currentId_ {EffectId::None};

    rt::ParameterSmoother ctrl1_, ctrl2_, ctrl3_;
};

}  // namespace sp303::fx
