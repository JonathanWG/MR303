#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sp303/Types.h"
#include "sp303/rt/RtSafe.h"
#include "sp303/sequencer/Quantize.h"

namespace sp303::seq {

// ---------------------------------------------------------------------------
// Pattern sequencer.
//
// SCOPE: deliberately OUT of the MVP. In a DAW the host's own sequencer covers
// most of this, so it is the first thing to cut. The types are here so the
// state format is stable from v0.1 and adding the sequencer later does not
// break every saved project.
//
// HOST SYNC is the hard part, not the note storage. The transport must survive
// scrubbing, loop jumps and tempo changes that land mid-block. Design in
// docs/ARCHITECTURE.md.
// ---------------------------------------------------------------------------

struct Event {
    double        positionInQuarters {0.0};  // from pattern start
    std::uint8_t  padIndex           {0};
    std::uint8_t  bank               {0};
    float         velocity           {1.0f};  // Modern mode only - HW has none
};

class Pattern {
public:
    void  setBars(int bars) noexcept;
    int   bars() const noexcept { return bars_; }
    double lengthInQuarters() const noexcept { return bars_ * 4.0; }

    // Off the audio thread - allocates.
    void addEvent(const Event& e);
    void clear();

    const std::vector<Event>& events() const noexcept { return events_; }
    bool empty() const noexcept { return events_.empty(); }

private:
    std::vector<Event> events_;
    int bars_ {kMinPatternBars};
};

// ---------------------------------------------------------------------------
// Transport position handed in by the host each block.
// ---------------------------------------------------------------------------
struct TransportInfo {
    double bpm              {120.0};
    double positionInQuarters {0.0};
    bool   isPlaying        {false};
    bool   didJump          {false};  // set when the host scrubbed or looped
};

class Sequencer {
public:
    void prepare(double sampleRate) noexcept;
    SP303_RT void reset() noexcept;

    SP303_RT void setTransport(const TransportInfo& info) noexcept;
    SP303_RT void setQuantize(Grid grid, double swingPercent) noexcept;

    // TODO(phase-3): advance the playhead by `numFrames` and emit the pad
    // triggers that fall inside the block, sample-accurately.
    // Returns the number of events emitted into `out`.
    SP303_RT int  processBlock(int numFrames, Event* out, int maxEvents) noexcept;

    Pattern&       pattern(int index) noexcept;
    const Pattern& pattern(int index) const noexcept;

    void setCurrentPattern(int index) noexcept;
    int  currentPattern() const noexcept { return currentPattern_; }

private:
    std::array<Pattern, kMaxPatterns> patterns_ {};
    int    currentPattern_ {0};
    double sampleRate_     {44100.0};
    double playhead_       {0.0};   // in quarter notes
    Grid   grid_           {Grid::Sixteenth};
    double swingPercent_   {50.0};
    TransportInfo transport_ {};
};

}  // namespace sp303::seq
