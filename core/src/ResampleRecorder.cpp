#include "sp303/ResampleRecorder.h"

#include <algorithm>
#include <cmath>

namespace sp303 {

void ResampleRecorder::prepare(double sampleRate, double maxSeconds) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto frames = static_cast<int>(sampleRate_ * std::max(maxSeconds, 1.0));
    capacity_ = frames;

    left_.assign(static_cast<std::size_t>(capacity_), 0.0f);
    right_.assign(static_cast<std::size_t>(capacity_), 0.0f);

    reset();
}

void ResampleRecorder::arm(int destinationSlot, Source source) noexcept {
    if (destinationSlot < 0 || destinationSlot >= kNumSlots) return;

    // Source before state: the audio thread only acts on the source after it
    // has seen Armed, so it can never pair a fresh state with a stale source.
    slot_.store(destinationSlot, std::memory_order_release);
    written_.store(0, std::memory_order_release);
    source_.store(source, std::memory_order_release);
    state_.store(State::Armed, std::memory_order_release);
}

void ResampleRecorder::cancel() noexcept {
    // State first, for the same reason arm() sets it last.
    state_.store(State::Idle, std::memory_order_release);
    source_.store(Source::Output, std::memory_order_release);
    slot_.store(-1, std::memory_order_release);
    written_.store(0, std::memory_order_release);
}

void ResampleRecorder::reset() noexcept {
    cancel();
}

const float* ResampleRecorder::channel(int index) const noexcept {
    if (index == 0) return left_.data();
    if (index == 1) return right_.data();
    return nullptr;
}

float ResampleRecorder::fillFraction() const noexcept {
    if (capacity_ <= 0) return 0.0f;
    return static_cast<float>(written_.load(std::memory_order_acquire))
         / static_cast<float>(capacity_);
}

void ResampleRecorder::start() noexcept {
    // Only Armed can begin. Guarding this stops a stray REC from overwriting a
    // finished take before the UI has collected it.
    auto expected = State::Armed;
    state_.compare_exchange_strong(expected, State::Recording,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire);
}

void ResampleRecorder::stop() noexcept {
    auto expected = State::Recording;
    state_.compare_exchange_strong(expected, State::Finished,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire);
}

void ResampleRecorder::write(const float* left, const float* right,
                             int numFrames) noexcept {
    if (left == nullptr || right == nullptr || numFrames <= 0) return;
    if (state_.load(std::memory_order_acquire) != State::Recording) return;

    const int offset = written_.load(std::memory_order_relaxed);
    const int room   = capacity_ - offset;
    if (room <= 0) { stop(); return; }

    // Write what fits, then stop. Truncating the tail is the honest behaviour
    // when memory runs out - the alternative would be dropping the whole take.
    const int count = std::min(numFrames, room);

    std::copy_n(left,  count, left_.begin()  + offset);
    std::copy_n(right, count, right_.begin() + offset);

    written_.store(offset + count, std::memory_order_release);

    if (count < numFrames) stop();
}

}  // namespace sp303
