#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "sp303/rt/RtSafe.h"

namespace sp303::rt {

// ---------------------------------------------------------------------------
// A fixed set of "the audio thread is currently using this address" flags.
//
// WHY THIS EXISTS
//
// The obvious way to hand sample audio to the audio thread is an atomic
// shared_ptr: the loader swaps one in, the audio thread copies it out, and the
// reference count keeps the data alive for as long as a voice is playing it.
//
// That was the original design, and it does not work. Measured on MSVC 19.44 /
// x64, `std::atomic<std::shared_ptr<T>>::is_lock_free()` is FALSE - the
// implementation falls back to an internal mutex, so every load takes a lock on
// the real-time thread. It also puts the last reference drop, and therefore
// `operator delete`, on the audio thread whenever a voice finishes.
//
// So the audio thread reads a plain raw pointer, which is always lock-free, and
// publishes which pointer it is holding here. The message thread frees a
// retired buffer only once no hazard points at it. Nothing on the audio thread
// allocates, locks, or frees.
//
// PROTOCOL
//
//   audio thread   protect(i, p) / clear(i)   - never blocks
//   message thread isProtected(p)             - scan before freeing
//
// The scan is O(N) over a handful of slots, on the message thread, at UI rates.
// It is not worth a cleverer structure.
// ---------------------------------------------------------------------------
template <std::size_t N>
class HazardPointers {
public:
    static constexpr std::size_t size() noexcept { return N; }

    // Audio thread. `pointer` may be nullptr, meaning "holding nothing".
    SP303_RT void protect(std::size_t index, const void* pointer) noexcept {
        if (index >= N) return;
        slots_[index].store(pointer, std::memory_order_release);
    }

    SP303_RT void clear(std::size_t index) noexcept {
        protect(index, nullptr);
    }

    SP303_RT void clearAll() noexcept {
        for (auto& slot : slots_)
            slot.store(nullptr, std::memory_order_release);
    }

    // Message thread. True while the audio thread may still dereference
    // `pointer`, so the caller must not free it yet.
    bool isProtected(const void* pointer) const noexcept {
        if (pointer == nullptr) return false;
        for (const auto& slot : slots_)
            if (slot.load(std::memory_order_acquire) == pointer) return true;
        return false;
    }

    const void* at(std::size_t index) const noexcept {
        return index < N ? slots_[index].load(std::memory_order_acquire) : nullptr;
    }

    // A raw pointer atomic is lock-free on every platform this targets. If it
    // ever is not, the whole scheme is pointless and the build should say so.
    static bool isLockFree() noexcept {
        std::atomic<const void*> probe {nullptr};
        return probe.is_lock_free();
    }

private:
    std::array<std::atomic<const void*>, N> slots_ {};
};

}  // namespace sp303::rt
