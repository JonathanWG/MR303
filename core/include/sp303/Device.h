#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "sp303/ResampleRecorder.h"
#include "sp303/SampleBuffer.h"
#include "sp303/Types.h"
#include "sp303/dsp/Decimator.h"
#include "sp303/dsp/Quantizer.h"
#include "sp303/fx/EffectRack.h"
#include "sp303/rt/RtSafe.h"
#include "sp303/rt/SpscQueue.h"
#include "sp303/sequencer/Sequencer.h"
#include "sp303/voice/Pad.h"
#include "sp303/voice/VoiceManager.h"

namespace sp303 {

// ---------------------------------------------------------------------------
// Commands from the UI thread to the audio thread.
//
// Trivially copyable by design - this is what SpscQueue carries. Anything that
// needs to hand over ownership (a loaded sample) does it via SampleSlot's
// atomic pointer swap and only sends the slot index through here.
// ---------------------------------------------------------------------------
struct Command {
    enum class Type : std::uint8_t {
        NoteOn,
        NoteOff,
        AllNotesOff,
        SelectBank,
        SelectEffect,
        SetPadSettings,
        StartResample,
        StopResample,
        SetQualityMode,
        SetFidelityMode
    };

    Type          type       {Type::AllNotesOff};
    std::int32_t  intValue   {0};
    float         floatValue {0.0f};
    Pad           pad        {};
};

// Audio -> UI. Lossy: dropping a frame of LED state is invisible.
struct Telemetry {
    std::uint8_t padStates    {0};  // bit per pad
    float        outputPeakL  {0.0f};
    float        outputPeakR  {0.0f};
    std::int32_t activeVoices {0};
};

// ---------------------------------------------------------------------------
// The whole instrument. This is the object the plugin processor owns.
//
// Everything reachable from processBlock() is real-time safe. Loading samples,
// rendering resamples offline and building patterns happen elsewhere and
// arrive through the command queue or an atomic slot swap.
// ---------------------------------------------------------------------------
class Device {
public:
    Device();

    // Allocates. Called from the plugin's prepareToPlay().
    void prepare(double sampleRate, int maxBlockSize);

    SP303_RT void reset() noexcept;

    // Main entry point from the audio callback.
    //
    // `inputLeft/inputRight` are the host's audio input for this block, or
    // nullptr when the host provides none. They are only read while a REC
    // take is armed or running; the rest of the time the instrument generates
    // from silence as before. In-place hosts pass the same pointers for input
    // and output - that is expected, and the input is read before the bus is
    // touched. See "Sampling into the plugin" in docs/ARCHITECTURE.md.
    SP303_RT void processBlock(const float* inputLeft, const float* inputRight,
                               float* left, float* right, int numFrames,
                               const seq::TransportInfo& transport) noexcept;

    // No host input. Kept for callers that only generate - tests, the Python
    // harness - and for hosts that give a synth no input bus.
    SP303_RT void processBlock(float* left, float* right, int numFrames,
                               const seq::TransportInfo& transport) noexcept {
        processBlock(nullptr, nullptr, left, right, numFrames, transport);
    }

    // --- Thread-safe channels -------------------------------------------------
    rt::SpscQueue<Command>&   commandQueue()   noexcept { return commands_; }
    rt::SpscQueue<Telemetry>& telemetryQueue() noexcept { return telemetry_; }

    // --- Sample memory (UI/loader thread) ------------------------------------
    //
    // Replacing a slot does NOT free what was there. The audio thread holds raw
    // pointers into sample memory (see SampleBuffer.h for why it cannot hold
    // shared_ptrs), so the displaced buffer goes onto a retirement list and is
    // freed later, here on the message thread, once no voice can still be
    // reading it.
    void setSlotSample(int slotIndex, SampleBufferPtr buffer);
    SampleBufferPtr slotSample(int slotIndex) const;

    // Message thread. Frees every retired buffer that is provably unreachable
    // from the audio thread. Cheap and safe to call often - the plugin runs it
    // on its 20 Hz timer. Returns how many buffers it freed.
    int collectRetiredSamples();

    // Diagnostics: buffers waiting to be freed. Should return to 0 shortly
    // after the voices using them stop.
    int pendingRetiredSamples() const noexcept {
        return static_cast<int>(retired_.size());
    }

    // Blocks processed since construction. The retirement rule keys off this:
    // a buffer retired during block N cannot be freed until block N has
    // finished, which is exactly when the counter moves off N.
    std::uint64_t blockCount() const noexcept {
        return blockCounter_.load(std::memory_order_acquire);
    }

    // --- Pad settings ---------------------------------------------------------
    Pad&       pad(int bank, int padIndex) noexcept;
    const Pad& pad(int bank, int padIndex) const noexcept;

    // --- Modes ----------------------------------------------------------------
    void setQualityMode(QualityMode mode);
    void setFidelityMode(FidelityMode mode) noexcept;
    QualityMode  qualityMode()  const noexcept { return quality_; }
    FidelityMode fidelityMode() const noexcept { return fidelity_; }

    seq::Sequencer&   sequencer()  noexcept { return sequencer_; }
    fx::EffectRack&   effectRack() noexcept { return effects_; }
    VoiceManager&     voices()     noexcept { return voices_; }
    ResampleRecorder& resampler()  noexcept { return resampler_; }

    // Arm a capture from the UI thread. The buffer is already allocated, so
    // these only flip atomics - the audio thread starts on the next
    // StartResample command and stops on StopResample, whichever the source.
    //
    //   armResample       RESAMPLE: records the instrument's own output
    //   armInputSampling  REC:      records the host's audio input, raw, and
    //                               passes it through to the output meanwhile
    void armResample(int destinationSlot) noexcept;
    void armInputSampling(int destinationSlot) noexcept;

    // CTRL 1/2/3, normalised. Written by the UI, read by the audio thread.
    std::atomic<float> ctrl1 {0.5f};
    std::atomic<float> ctrl2 {0.0f};
    std::atomic<float> ctrl3 {0.0f};

private:
    SP303_RT void drainCommands() noexcept;
    SP303_RT void publishTelemetry(const float* left, const float* right,
                                   int numFrames) noexcept;

    // A buffer that has been displaced from its slot and is waiting out the
    // audio thread before it can be freed.
    struct RetiredSample {
        SampleBufferPtr buffer;
        std::uint64_t   retiredAtBlock {0};
    };

    std::array<SampleSlot, kNumSlots> slots_ {};
    std::array<Pad,        kNumSlots> pads_  {};

    // Message thread only. Grows while audio is stopped and samples are being
    // replaced, which is bounded by user actions; drains on the next block.
    std::vector<RetiredSample> retired_;

    std::atomic<std::uint64_t> blockCounter_ {0};

    VoiceManager     voices_ {};
    fx::EffectRack   effects_ {};
    seq::Sequencer   sequencer_ {};
    ResampleRecorder resampler_ {};
    dsp::Decimator  decimatorL_ {}, decimatorR_ {};
    dsp::Quantizer  quantizerL_ {}, quantizerR_ {};

    rt::SpscQueue<Command>   commands_  {256};
    rt::SpscQueue<Telemetry> telemetry_ {64};

    double       sampleRate_  {44100.0};
    int          activeBank_  {0};
    QualityMode  quality_     {QualityMode::Standard};
    FidelityMode fidelity_    {FidelityMode::Authentic};
};

}  // namespace sp303
