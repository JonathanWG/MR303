#include "sp303/sequencer/Sequencer.h"

#include <algorithm>

namespace sp303::seq {

// ---------------------------------------------------------------------------
// Pattern
// ---------------------------------------------------------------------------
void Pattern::setBars(int bars) noexcept {
    bars_ = std::clamp(bars, kMinPatternBars, kMaxPatternBars);
}

void Pattern::addEvent(const Event& e) {
    events_.push_back(e);
    std::sort(events_.begin(), events_.end(),
              [](const Event& a, const Event& b) {
                  return a.positionInQuarters < b.positionInQuarters;
              });
}

void Pattern::clear() {
    events_.clear();
}

// ---------------------------------------------------------------------------
// Sequencer
// ---------------------------------------------------------------------------
void Sequencer::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Sequencer::reset() noexcept {
    playhead_ = 0.0;
}

void Sequencer::setTransport(const TransportInfo& info) noexcept {
    // A host jump (scrub, loop wrap, locate) must not be integrated as if it
    // were elapsed musical time, or every jump fires a burst of stale events.
    if (info.didJump || !transport_.isPlaying)
        playhead_ = info.positionInQuarters;

    transport_ = info;
}

void Sequencer::setQuantize(Grid grid, double swingPercent) noexcept {
    grid_         = grid;
    swingPercent_ = std::clamp(swingPercent, 50.0, 75.0);
}

int Sequencer::processBlock(int numFrames, Event* out, int maxEvents) noexcept {
    // TODO(phase-3): not implemented. Deliberately a no-op rather than a
    // half-working playhead - a sequencer that fires events at approximately
    // the right time is worse than one that is honestly switched off.
    //
    // Implementation sketch:
    //   1. blockLengthInQuarters = numFrames / sampleRate * bpm / 60
    //   2. window = [playhead_, playhead_ + blockLengthInQuarters)
    //   3. wrap the window against pattern.lengthInQuarters()
    //   4. emit events inside the window, converting each to a frame offset
    //      so triggers are sample-accurate rather than block-quantised
    //   5. advance playhead_
    (void)numFrames;
    (void)out;
    (void)maxEvents;
    return 0;
}

Pattern& Sequencer::pattern(int index) noexcept {
    const auto i = std::clamp(index, 0, kMaxPatterns - 1);
    return patterns_[static_cast<std::size_t>(i)];
}

const Pattern& Sequencer::pattern(int index) const noexcept {
    const auto i = std::clamp(index, 0, kMaxPatterns - 1);
    return patterns_[static_cast<std::size_t>(i)];
}

void Sequencer::setCurrentPattern(int index) noexcept {
    currentPattern_ = std::clamp(index, 0, kMaxPatterns - 1);
}

}  // namespace sp303::seq
