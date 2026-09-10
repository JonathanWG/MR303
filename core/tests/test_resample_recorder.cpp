#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "sp303/ResampleRecorder.h"

using namespace sp303;
using Catch::Approx;
using State = ResampleRecorder::State;

namespace {

// A recorder with a deliberately tiny budget, so the overflow path is reachable
// without allocating seconds of audio in a unit test.
//
// prepare() clamps maxSeconds to a 1 s floor, so the capacity is driven by the
// sample rate instead: 1 s at 100 Hz is 100 frames.
//
// Prepared in place rather than returned by value: ResampleRecorder holds
// std::atomic members, so it is neither copyable nor movable.
void prepareRecorder(ResampleRecorder& recorder,
                     double sampleRate = 100.0,
                     double seconds = 1.0) {
    recorder.prepare(sampleRate, seconds);
}

std::vector<float> ramp(int numFrames, float first = 0.0f) {
    std::vector<float> data(static_cast<std::size_t>(numFrames));
    for (int i = 0; i < numFrames; ++i)
        data[static_cast<std::size_t>(i)] = first + static_cast<float>(i);
    return data;
}

}  // namespace

TEST_CASE("A prepared recorder starts idle and empty", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);

    CHECK(recorder.state() == State::Idle);
    CHECK(recorder.destinationSlot() == -1);
    CHECK(recorder.capturedFrames() == 0);
    CHECK(recorder.fillFraction() == Approx(0.0f));
    CHECK(recorder.numChannels() == 2);
    CHECK(recorder.sampleRate() == Approx(100.0));

    CHECK(recorder.channel(0) != nullptr);
    CHECK(recorder.channel(1) != nullptr);
    CHECK(recorder.channel(2) == nullptr);
    CHECK(recorder.channel(-1) == nullptr);
}

TEST_CASE("arm selects a destination and moves to Armed", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);

    recorder.arm(5);
    CHECK(recorder.state() == State::Armed);
    CHECK(recorder.destinationSlot() == 5);
    CHECK(recorder.capturedFrames() == 0);
    CHECK_FALSE(recorder.isRecording());
}

TEST_CASE("arm rejects an out-of-range slot", "[resample]") {
    // A bad slot index must not leave the recorder armed at a destination the
    // collector cannot write to.
    ResampleRecorder recorder;
    prepareRecorder(recorder);

    recorder.arm(-1);
    CHECK(recorder.state() == State::Idle);

    recorder.arm(kNumSlots);
    CHECK(recorder.state() == State::Idle);

    recorder.arm(kNumSlots - 1);
    CHECK(recorder.state() == State::Armed);
    CHECK(recorder.destinationSlot() == kNumSlots - 1);
}

TEST_CASE("start() only leaves the Armed state", "[resample]") {
    // This is the guard that stops a stray REC from clobbering a finished take
    // before the message thread has collected it.
    ResampleRecorder recorder;
    prepareRecorder(recorder);

    SECTION("Idle ignores start") {
        recorder.start();
        CHECK(recorder.state() == State::Idle);
        CHECK_FALSE(recorder.isRecording());
    }

    SECTION("Armed begins recording") {
        recorder.arm(0);
        recorder.start();
        CHECK(recorder.state() == State::Recording);
        CHECK(recorder.isRecording());
    }

    SECTION("Recording is not restarted") {
        recorder.arm(0);
        recorder.start();

        const auto left = ramp(10, 1.0f);
        recorder.write(left.data(), left.data(), 10);
        REQUIRE(recorder.capturedFrames() == 10);

        recorder.start();  // stray REC
        CHECK(recorder.state() == State::Recording);
        CHECK(recorder.capturedFrames() == 10);  // not rewound
    }

    SECTION("Finished is not restarted") {
        recorder.arm(0);
        recorder.start();
        recorder.stop();
        REQUIRE(recorder.state() == State::Finished);

        recorder.start();
        CHECK(recorder.state() == State::Finished);
    }
}

TEST_CASE("stop() only leaves the Recording state", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);

    SECTION("Idle stays idle") {
        recorder.stop();
        CHECK(recorder.state() == State::Idle);
    }

    SECTION("Armed stays armed - a stop before the first sample is not a take") {
        recorder.arm(3);
        recorder.stop();
        CHECK(recorder.state() == State::Armed);
    }
}

TEST_CASE("write appends across blocks and preserves both channels", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);
    recorder.arm(2);
    recorder.start();

    const auto first  = ramp(8, 0.0f);    // 0..7
    const auto second = ramp(8, 100.0f);  // 100..107

    recorder.write(first.data(), second.data(), 8);
    CHECK(recorder.capturedFrames() == 8);

    recorder.write(second.data(), first.data(), 8);
    CHECK(recorder.capturedFrames() == 16);
    CHECK(recorder.state() == State::Recording);

    const float* left  = recorder.channel(0);
    const float* right = recorder.channel(1);
    REQUIRE(left  != nullptr);
    REQUIRE(right != nullptr);

    CHECK(left[0]   == Approx(0.0f));
    CHECK(left[7]   == Approx(7.0f));
    CHECK(left[8]   == Approx(100.0f));
    CHECK(left[15]  == Approx(107.0f));

    CHECK(right[0]  == Approx(100.0f));
    CHECK(right[7]  == Approx(107.0f));
    CHECK(right[8]  == Approx(0.0f));
    CHECK(right[15] == Approx(7.0f));

    CHECK(recorder.fillFraction() == Approx(16.0f / 100.0f));
}

TEST_CASE("write is ignored unless the recorder is Recording", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);
    const auto data = ramp(8, 1.0f);

    recorder.write(data.data(), data.data(), 8);
    CHECK(recorder.capturedFrames() == 0);

    recorder.arm(0);
    recorder.write(data.data(), data.data(), 8);
    CHECK(recorder.capturedFrames() == 0);

    recorder.start();
    recorder.stop();
    recorder.write(data.data(), data.data(), 8);
    CHECK(recorder.capturedFrames() == 0);
}

TEST_CASE("write rejects degenerate arguments", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);
    recorder.arm(0);
    recorder.start();

    const auto data = ramp(8, 1.0f);

    recorder.write(nullptr, data.data(), 8);
    recorder.write(data.data(), nullptr, 8);
    recorder.write(data.data(), data.data(), 0);
    recorder.write(data.data(), data.data(), -4);

    CHECK(recorder.capturedFrames() == 0);
    CHECK(recorder.state() == State::Recording);
}

TEST_CASE("A block that overruns the buffer is truncated and stops the take",
          "[resample]") {
    // Running out of sampling time is a hardware behaviour, not an error. The
    // honest response is to keep what fits rather than drop the whole take.
    ResampleRecorder recorder;
    prepareRecorder(recorder);  // 100 frames
    recorder.arm(1);
    recorder.start();

    const auto block = ramp(60, 0.0f);

    recorder.write(block.data(), block.data(), 60);
    REQUIRE(recorder.capturedFrames() == 60);
    REQUIRE(recorder.state() == State::Recording);

    recorder.write(block.data(), block.data(), 60);  // only 40 fit

    CHECK(recorder.capturedFrames() == 100);
    CHECK(recorder.state() == State::Finished);
    CHECK_FALSE(recorder.isRecording());
    CHECK(recorder.fillFraction() == Approx(1.0f));

    const float* left = recorder.channel(0);
    REQUIRE(left != nullptr);
    CHECK(left[59] == Approx(59.0f));
    CHECK(left[60] == Approx(0.0f));   // second block, first frame
    CHECK(left[99] == Approx(39.0f));  // truncated at frame 40 of the block
}

TEST_CASE("Writing after the buffer is exactly full stops the take", "[resample]") {
    // Filling to the last frame is not itself an overflow, so the recorder is
    // still Recording. The next block finds no room and ends the take.
    ResampleRecorder recorder;
    prepareRecorder(recorder);  // 100 frames
    recorder.arm(0);
    recorder.start();

    const auto block = ramp(100, 0.0f);
    recorder.write(block.data(), block.data(), 100);

    CHECK(recorder.capturedFrames() == 100);
    CHECK(recorder.state() == State::Recording);

    recorder.write(block.data(), block.data(), 8);
    CHECK(recorder.capturedFrames() == 100);
    CHECK(recorder.state() == State::Finished);
}

TEST_CASE("A full arm/start/write/stop cycle exposes the take to the collector",
          "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);

    recorder.arm(9);
    recorder.start();

    const auto block = ramp(32, 1.0f);
    recorder.write(block.data(), block.data(), 32);
    recorder.stop();

    // What the message-thread collector reads: a level-triggered state, the
    // destination, and a frame count. Nothing is delivered by message, so a
    // dropped notification cannot lose the take.
    REQUIRE(recorder.state() == State::Finished);
    CHECK(recorder.destinationSlot() == 9);
    CHECK(recorder.capturedFrames() == 32);
    CHECK(recorder.channel(0)[31] == Approx(32.0f));

    recorder.reset();
    CHECK(recorder.state() == State::Idle);
    CHECK(recorder.destinationSlot() == -1);
    CHECK(recorder.capturedFrames() == 0);
}

TEST_CASE("cancel abandons an in-flight take", "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder);
    recorder.arm(4);
    recorder.start();

    const auto block = ramp(16, 1.0f);
    recorder.write(block.data(), block.data(), 16);
    REQUIRE(recorder.capturedFrames() == 16);

    recorder.cancel();

    CHECK(recorder.state() == State::Idle);
    CHECK(recorder.destinationSlot() == -1);
    CHECK(recorder.capturedFrames() == 0);

    // And a write that races the cancel is dropped rather than resurrecting it.
    recorder.write(block.data(), block.data(), 16);
    CHECK(recorder.capturedFrames() == 0);
    CHECK(recorder.state() == State::Idle);
}

TEST_CASE("prepare resizes the capture budget and clears any previous take",
          "[resample]") {
    ResampleRecorder recorder;
    prepareRecorder(recorder, 100.0, 1.0);
    recorder.arm(0);
    recorder.start();

    const auto block = ramp(50, 1.0f);
    recorder.write(block.data(), block.data(), 50);
    REQUIRE(recorder.capturedFrames() == 50);

    // Changing quality mode re-prepares the recorder. It must come back Idle.
    recorder.prepare(100.0, 2.0);

    CHECK(recorder.state() == State::Idle);
    CHECK(recorder.capturedFrames() == 0);
    CHECK(recorder.sampleRate() == Approx(100.0));

    recorder.arm(0);
    recorder.start();

    const auto longBlock = ramp(200, 0.0f);
    recorder.write(longBlock.data(), longBlock.data(), 200);

    CHECK(recorder.capturedFrames() == 200);  // 2 s at 100 Hz
    CHECK(recorder.state() == State::Recording);  // exactly full is not overflow
}
