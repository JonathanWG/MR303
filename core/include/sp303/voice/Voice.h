#pragma once

#include "sp303/SampleBuffer.h"
#include "sp303/dsp/Interpolator.h"
#include "sp303/rt/RtSafe.h"
#include "sp303/voice/Pad.h"

namespace sp303 {

// ---------------------------------------------------------------------------
// One of the 8 polyphonic voices.
//
// A voice owns no memory. It holds a RAW pointer to an immutable SampleBuffer
// for the duration of a note.
//
// Raw, not shared_ptr, and that is deliberate: a shared_ptr copy would put the
// last reference drop - and so `operator delete` - on the audio thread the
// moment a voice finishes. Lifetime is instead guaranteed from the outside.
// VoiceManager publishes what every voice holds into rt::HazardPointers at the
// end of each block, and the message thread refuses to free anything a hazard
// still names. See SampleBuffer.h and Device::collectRetiredSamples().
// ---------------------------------------------------------------------------
class Voice {
public:
    void prepare(double hostSampleRate) noexcept;

    // `buffer` must outlive the note. The caller guarantees that; see the
    // hazard-pointer protocol above.
    SP303_RT void start(const Pad& pad, const SampleBuffer* buffer, int padIndex) noexcept;

    // Pad released. In Gate mode this stops the voice; in Trigger mode it is
    // ignored, which is exactly the hardware behaviour.
    SP303_RT void release() noexcept;

    // Immediate stop, no fade. Used by CANCEL and by voice stealing.
    SP303_RT void stop() noexcept;

    SP303_RT bool isActive()  const noexcept { return active_; }

    // What this voice is reading, or nullptr. Read by VoiceManager to publish
    // the hazard set; it is the only thing keeping the buffer alive.
    SP303_RT const SampleBuffer* bufferPointer() const noexcept { return buffer_; }

    SP303_RT int  padIndex()  const noexcept { return padIndex_; }
    SP303_RT std::uint64_t startOrder() const noexcept { return startOrder_; }

    // Renders one frame. `outL`/`outR` are accumulated into, not overwritten.
    SP303_RT void renderNextFrame(float& outL, float& outR) noexcept;

    void setInterpolationMode(dsp::InterpolationMode mode) noexcept { interp_ = mode; }

    // Monotonic counter used for oldest-first voice stealing.
    static void resetGlobalOrder() noexcept { nextOrder_ = 0; }

private:
    SP303_RT void advance() noexcept;

    const SampleBuffer* buffer_ {nullptr};
    Pad                 pad_ {};

    bool          active_     {false};
    bool          released_   {false};
    int           padIndex_   {-1};
    double        position_   {0.0};
    double        increment_  {1.0};
    std::uint32_t regionStart_{0};
    std::uint32_t regionEnd_  {0};
    std::uint64_t startOrder_ {0};

    double hostRate_ {44100.0};
    dsp::InterpolationMode interp_ {dsp::InterpolationMode::Linear};

    static inline std::uint64_t nextOrder_ {0};
};

}  // namespace sp303
