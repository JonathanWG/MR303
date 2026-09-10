#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <vector>

#include "sp303/rt/RtSafe.h"

namespace sp303::rt {

// ---------------------------------------------------------------------------
// Single-producer / single-consumer lock-free ring buffer.
//
// This is the only sanctioned channel between the UI thread and the audio
// thread. Capacity is fixed at construction (allocated once, off the audio
// thread); push and pop never allocate and never block.
//
// Two instances are used per plugin:
//   UI    -> audio : discrete commands (load pad, change mode, arm record)
//   audio -> UI    : telemetry (pad LEDs, playhead, meters) - lossy by design
//
// Continuous parameters do NOT go through here. They use ParameterSmoother
// over a std::atomic<float>, so the audio thread always reads the newest value
// rather than draining a backlog of stale ones.
// ---------------------------------------------------------------------------
template <typename T>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscQueue payloads must be trivially copyable - no strings, "
                  "no owning pointers, nothing with a destructor that matters.");

public:
    // `capacity` is rounded up to a power of two so the modulo is a mask.
    explicit SpscQueue(std::size_t capacity) {
        std::size_t n = 1;
        while (n < capacity) n <<= 1;
        buffer_.resize(n);
        mask_ = n - 1;
    }

    // Producer side. Returns false if the queue is full (caller decides whether
    // that is fatal or droppable).
    SP303_RT bool push(const T& item) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto next = (w + 1) & mask_;
        if (next == read_.load(std::memory_order_acquire))
            return false;  // full
        buffer_[w] = item;
        write_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side.
    SP303_RT bool pop(T& out) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;  // empty
        out = buffer_[r];
        read_.store((r + 1) & mask_, std::memory_order_release);
        return true;
    }

    SP303_RT bool empty() const noexcept {
        return read_.load(std::memory_order_acquire) ==
               write_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const noexcept { return buffer_.size() - 1; }

private:
    std::vector<T> buffer_;
    std::size_t    mask_ {0};

    // Kept on separate cache lines: otherwise the producer's store to write_
    // invalidates the consumer's cache line holding read_ on every push.
    alignas(64) std::atomic<std::size_t> write_ {0};
    alignas(64) std::atomic<std::size_t> read_  {0};
};

}  // namespace sp303::rt
