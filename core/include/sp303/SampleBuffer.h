#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sp303/rt/RtSafe.h"

namespace sp303 {

// ---------------------------------------------------------------------------
// Immutable audio payload for one pad.
//
// IMMUTABILITY IS THE THREADING CONTRACT. Once constructed, the data never
// changes. The loader thread builds a fresh SampleBuffer, and the audio thread
// picks it up via an atomic pointer swap (see SampleSlot below). That is what
// lets the audio thread read sample data without a single lock.
//
// Editing START/END does NOT mutate this - those live on the Pad, so trimming
// is free and non-destructive.
// ---------------------------------------------------------------------------
class SampleBuffer {
public:
    SampleBuffer() = default;
    SampleBuffer(std::vector<std::vector<float>> channels,
                 double sourceSampleRate,
                 std::string name);

    SP303_RT bool  isEmpty()      const noexcept { return numFrames_ == 0; }
    SP303_RT int   numFrames()    const noexcept { return numFrames_; }
    SP303_RT int   numChannels()  const noexcept { return numChannels_; }
    SP303_RT double sourceRate()  const noexcept { return sourceRate_; }

    // Returns nullptr for an out-of-range channel. Callers on the audio thread
    // must null-check; a missing channel is not an error worth branching on
    // twice.
    SP303_RT const float* channel(int index) const noexcept {
        if (index < 0 || index >= numChannels_) return nullptr;
        return channels_[static_cast<std::size_t>(index)].data();
    }

    const std::string& name() const noexcept { return name_; }

    // Length-derived BPM, the way the hardware's TIME/BPM display works.
    // This is arithmetic on duration, NOT beat detection - it assumes the
    // sample is exactly `bars` bars long.
    double estimateBpm(int bars = 1, int beatsPerBar = 4) const noexcept;

private:
    std::vector<std::vector<float>> channels_;
    int         numFrames_   {0};
    int         numChannels_ {0};
    double      sourceRate_  {44100.0};
    std::string name_;
};

using SampleBufferPtr = std::shared_ptr<const SampleBuffer>;

// ---------------------------------------------------------------------------
// One slot in the sample memory, safe to swap while audio is running.
//
//   loader thread : publish(newBuffer) -> returns the buffer it displaced
//   audio  thread : load()             -> a raw pointer, valid for this block
//
// WHY NOT std::atomic<std::shared_ptr>
//
// That was the original design and it is wrong on the primary target. Measured
// on MSVC 19.44 / x64: `std::atomic<std::shared_ptr<T>>::is_lock_free()` is
// FALSE. The implementation falls back to an internal mutex, so `load()` blocks
// the audio thread on every pad hit - a direct violation of the one rule this
// project is built around. It also runs the last reference drop, and so
// `operator delete`, on the audio thread when a voice ends.
//
// A raw pointer atomic is lock-free everywhere, so the audio thread reads one
// of those instead. The cost is that lifetime is no longer automatic: the
// message thread owns the buffer, and it must not free a displaced one until
// the audio thread has provably stopped looking at it. That is what
// rt::HazardPointers and Device's retirement list are for.
//
// This class therefore does NOT free anything on publish. It hands the old
// buffer back and lets the caller decide when it dies.
// ---------------------------------------------------------------------------
class SampleSlot {
public:
    // Message thread. Returns the buffer that was displaced - RETIRE IT, do not
    // simply drop it: the audio thread may still be inside a block that loaded
    // its pointer. Ignoring the return value is a use-after-free waiting to
    // happen, hence [[nodiscard]].
    [[nodiscard]] SampleBufferPtr publish(SampleBufferPtr buffer) noexcept {
        // Publish first, then take ownership. Between the two, `buffer` still
        // holds the new one alive, so the pointer the audio thread can now see
        // is never dangling.
        published_.store(buffer.get(), std::memory_order_release);
        owner_.swap(buffer);
        return buffer;  // the previous occupant
    }

    // Audio thread. Valid until the message thread proves otherwise; the
    // caller must register a hazard if it intends to keep it past this block.
    SP303_RT const SampleBuffer* load() const noexcept {
        return published_.load(std::memory_order_acquire);
    }

    // Message thread. The owning handle, for state saving and the UI.
    SampleBufferPtr owned() const noexcept { return owner_; }

    bool isEmpty() const noexcept {
        return published_.load(std::memory_order_acquire) == nullptr;
    }

private:
    std::atomic<const SampleBuffer*> published_ {nullptr};

    // Message thread only. Keeps the published buffer alive; never touched
    // from the audio thread.
    SampleBufferPtr owner_;
};

}  // namespace sp303
