#pragma once

#include <array>

#include "sp303/SampleBuffer.h"
#include "sp303/Types.h"
#include "sp303/rt/HazardPointers.h"
#include "sp303/rt/RtSafe.h"
#include "sp303/voice/Pad.h"
#include "sp303/voice/Voice.h"

namespace sp303 {

// ---------------------------------------------------------------------------
// Fixed pool of 8 voices - matching the hardware's polyphony exactly.
//
// The 8-voice limit is not an arbitrary constraint to preserve for flavour: it
// is what makes live resampling a mixdown operation. Producers stack pads,
// hit RESAMPLE, and print the result. Raising the limit in Modern mode is
// supported (kMaxVoices is a compile-time cap on the pool, `polyphony_` is the
// runtime limit).
//
// It also owns the hazard set that keeps sample memory alive. Every voice holds
// a raw SampleBuffer pointer (see Voice.h for why), and render() republishes
// the whole set at the end of each block. The message thread will not free a
// retired buffer that any hazard still names.
// ---------------------------------------------------------------------------
class VoiceManager {
public:
    void prepare(double hostSampleRate) noexcept;

    // `buffer` must stay alive for the whole note. Device guarantees that by
    // retiring displaced buffers rather than freeing them.
    SP303_RT void noteOn(int padIndex, const Pad& pad, const SampleBuffer* buffer) noexcept;
    SP303_RT void noteOff(int padIndex) noexcept;
    SP303_RT void allNotesOff() noexcept;

    // Accumulates all active voices into the buffers. Does NOT clear them
    // first - the caller owns the mix bus.
    SP303_RT void render(float* left, float* right, int numFrames) noexcept;

    SP303_RT int activeVoiceCount() const noexcept;

    // Read by the message thread before freeing a retired buffer. Published by
    // render(), and by allNotesOff() so stopping everything releases memory at
    // the next collection rather than at the next block.
    const rt::HazardPointers<kMaxVoices>& hazards() const noexcept { return hazards_; }

    // Republishes what every voice currently holds. render() calls this; it is
    // exposed so a block that renders nothing can still refresh the set.
    SP303_RT void publishHazards() noexcept;

    void setPolyphony(int voices) noexcept;
    void setInterpolationMode(dsp::InterpolationMode mode) noexcept;

private:
    SP303_RT Voice* findFreeVoice() noexcept;
    SP303_RT Voice* stealOldestVoice() noexcept;

    std::array<Voice, kMaxVoices> voices_ {};
    rt::HazardPointers<kMaxVoices> hazards_ {};
    int  polyphony_ {kMaxVoices};
    dsp::InterpolationMode interp_ {dsp::InterpolationMode::Linear};
};

}  // namespace sp303
