#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "sp303/Types.h"
#include "sp303/rt/RtSafe.h"

namespace sp303 {

// ---------------------------------------------------------------------------
// Capture engine - recording audio into a pad.
//
// Two things feed it, and the hardware treats them as one operation with two
// entry points:
//
//   RESAMPLE   Source::Output   the instrument's own output, after the effect
//                               and the lo-fi stage. The feature the whole
//                               workflow turns on: print a filtered sample,
//                               then filter the print again.
//   REC        Source::Input    the host's audio input - the track the plugin
//                               sits on, or whatever the host routes into its
//                               input bus. The plugin's equivalent of plugging
//                               a turntable into LINE IN.
//
// The state machine is identical for both. Only where write() gets its audio
// from differs, and Device decides that by reading source().
//
// THREADING
//
//   UI thread     arm(slot, source) -> poll state() -> read frames -> reset()
//   audio thread  start() -> write() per block -> stop()
//
// The state is LEVEL-TRIGGERED, not an event: the UI polls for Finished rather
// than waiting for a message. A dropped "capture finished" notification would
// silently lose a take, and telemetry queues are allowed to drop.
//
// The buffer is allocated once in prepare(). write() never allocates - when it
// runs out of room it stops itself, which is exactly what the hardware does
// when it runs out of sampling time.
// ---------------------------------------------------------------------------
class ResampleRecorder {
public:
    enum class State : std::uint8_t {
        Idle,       // nothing happening
        Armed,      // destination chosen, waiting for REC
        Recording,  // capturing
        Finished    // capture complete, waiting for the UI to collect it
    };

    enum class Source : std::uint8_t {
        Output,  // RESAMPLE - what the instrument is playing
        Input    // REC      - what the host is feeding in
    };

    // Allocates. Call from prepareToPlay(), never while recording.
    void prepare(double sampleRate, double maxSeconds);

    // --- UI thread ----------------------------------------------------------

    // Chooses where the take lands and what it listens to. The default is the
    // instrument's output, which is what every caller that predates input
    // sampling meant.
    void arm(int destinationSlot, Source source = Source::Output) noexcept;
    void cancel() noexcept;

    State  state()  const noexcept { return state_.load(std::memory_order_acquire); }
    Source source() const noexcept { return source_.load(std::memory_order_acquire); }
    int    destinationSlot() const noexcept { return slot_.load(std::memory_order_acquire); }
    int    capturedFrames()  const noexcept { return written_.load(std::memory_order_acquire); }

    const float* channel(int index) const noexcept;
    int    numChannels() const noexcept { return 2; }
    double sampleRate()  const noexcept { return sampleRate_; }

    // How full the buffer is, 0..1 - drives a progress readout so the user is
    // not surprised when a long take stops on its own.
    float fillFraction() const noexcept;

    // Call after copying the audio out. Returns the recorder to Idle.
    void reset() noexcept;

    // --- audio thread -------------------------------------------------------
    SP303_RT void start() noexcept;
    SP303_RT void stop() noexcept;

    SP303_RT bool isRecording() const noexcept {
        return state_.load(std::memory_order_acquire) == State::Recording;
    }

    SP303_RT bool isRecordingFrom(Source which) const noexcept {
        return source() == which && isRecording();
    }

    // True from arming until the take ends, when the source is the input.
    // Device passes the input through to the output while this holds, so the
    // user hears what is about to be recorded - sampling standby, as on the
    // hardware.
    SP303_RT bool isListeningToInput() const noexcept {
        if (source() != Source::Input) return false;
        const auto s = state();
        return s == State::Armed || s == State::Recording;
    }

    // Appends a block. Stops automatically once the buffer is full.
    SP303_RT void write(const float* left, const float* right, int numFrames) noexcept;

private:
    std::vector<float> left_;
    std::vector<float> right_;

    std::atomic<State>  state_   {State::Idle};
    std::atomic<Source> source_  {Source::Output};
    std::atomic<int>    slot_    {-1};
    std::atomic<int>    written_ {0};

    int    capacity_   {0};
    double sampleRate_ {44100.0};
};

}  // namespace sp303
