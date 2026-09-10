#pragma once

#include "sp303/SampleBuffer.h"
#include "sp303/Types.h"

namespace sp303 {

// ---------------------------------------------------------------------------
// Per-pad settings. Cheap to copy, no audio data - the audio lives in the
// SampleSlot, referenced by index.
//
// START / END are frame offsets, not milliseconds. On the hardware these are
// dialled in by ear with the CTRL knobs because there is no waveform display;
// in Modern mode the GUI draws a waveform and lets you drag them. Same data
// either way.
// ---------------------------------------------------------------------------
struct Pad {
    int         slotIndex   {-1};        // index into SampleMemory, -1 = empty
    std::uint32_t startFrame {0};
    std::uint32_t endFrame   {0};        // 0 = play to end of buffer
    float       level        {1.0f};     // LEVEL

    TriggerMode trigger      {TriggerMode::Trigger};
    LoopMode    loop         {LoopMode::OneShot};
    bool        reverse      {false};
    bool        hold         {false};    // HOLD: keeps playing after release

    // Playback rate multiplier. Whether this shifts pitch (varispeed) or
    // preserves it (true time-stretch) is UNVERIFIED - see docs/HARDWARE_FACTS.md.
    // The voice engine currently implements varispeed; if Phase 0 shows the
    // hardware preserves pitch, a granular stage slots in behind this same field.
    float       speedRatio   {1.0f};

    bool isEmpty() const noexcept { return slotIndex < 0; }
};

}  // namespace sp303
