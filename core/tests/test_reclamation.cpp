#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "sp303/Device.h"
#include "sp303/SampleBuffer.h"
#include "sp303/rt/HazardPointers.h"

using namespace sp303;
using Catch::Approx;

namespace {

SampleBufferPtr makeBuffer(int frames, float value) {
    std::vector<std::vector<float>> channels(1);
    channels[0].assign(static_cast<std::size_t>(frames), value);
    return std::make_shared<const SampleBuffer>(std::move(channels), 44100.0, "b");
}

// Runs one audio block through the device. Every retirement decision keys off
// the block counter, so "how many blocks have run" is the unit these tests
// think in.
void runBlock(Device& device, int frames = 64) {
    std::vector<float> left(static_cast<std::size_t>(frames), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(frames), 0.0f);
    const seq::TransportInfo transport;
    device.processBlock(left.data(), right.data(), frames, transport);
}

void push(Device& device, Command::Type type, int value = 0) {
    Command command;
    command.type     = type;
    command.intValue = value;
    REQUIRE(device.commandQueue().push(command));
}

}  // namespace

// ===========================================================================
// HazardPointers
// ===========================================================================

TEST_CASE("Hazard slots start empty", "[rt][hazard]") {
    rt::HazardPointers<4> hazards;

    int dummy = 0;
    CHECK_FALSE(hazards.isProtected(&dummy));
    CHECK(hazards.at(0) == nullptr);
    CHECK(rt::HazardPointers<4>::size() == 4u);
}

TEST_CASE("A protected pointer is reported as in use", "[rt][hazard]") {
    rt::HazardPointers<4> hazards;
    int a = 0, b = 0;

    hazards.protect(2, &a);
    CHECK(hazards.isProtected(&a));
    CHECK_FALSE(hazards.isProtected(&b));
    CHECK(hazards.at(2) == &a);

    hazards.clear(2);
    CHECK_FALSE(hazards.isProtected(&a));
}

TEST_CASE("clearAll releases every hazard", "[rt][hazard]") {
    rt::HazardPointers<4> hazards;
    int a = 0, b = 0;

    hazards.protect(0, &a);
    hazards.protect(3, &b);
    REQUIRE(hazards.isProtected(&a));
    REQUIRE(hazards.isProtected(&b));

    hazards.clearAll();
    CHECK_FALSE(hazards.isProtected(&a));
    CHECK_FALSE(hazards.isProtected(&b));
}

TEST_CASE("A null pointer is never protected", "[rt][hazard]") {
    // Otherwise an inactive voice would appear to be holding "nothing" and
    // block collection forever.
    rt::HazardPointers<4> hazards;
    CHECK_FALSE(hazards.isProtected(nullptr));

    hazards.protect(1, nullptr);
    CHECK_FALSE(hazards.isProtected(nullptr));
}

TEST_CASE("Out-of-range hazard indices are ignored, not undefined",
          "[rt][hazard]") {
    rt::HazardPointers<2> hazards;
    int a = 0;

    hazards.protect(99, &a);
    CHECK_FALSE(hazards.isProtected(&a));
    CHECK(hazards.at(99) == nullptr);
}

TEST_CASE("The hazard pointer type is lock-free", "[rt][hazard]") {
    // If this ever fails the whole scheme is pointless: it exists only to keep
    // the audio thread off a mutex.
    CHECK(rt::HazardPointers<8>::isLockFree());
}

// ===========================================================================
// Device retirement
// ===========================================================================

TEST_CASE("Replacing a slot retires the old buffer instead of freeing it",
          "[device][rt]") {
    Device device;
    device.prepare(44100.0, 64);

    auto first = makeBuffer(1000, 1.0f);
    device.setSlotSample(0, first);
    CHECK(device.pendingRetiredSamples() == 0);

    auto second = makeBuffer(1000, 2.0f);
    device.setSlotSample(0, second);

    // Displaced, not destroyed: a block in flight could still be holding it.
    CHECK(device.pendingRetiredSamples() == 1);
    CHECK(device.slotSample(0).get() == second.get());
}

TEST_CASE("A retired buffer is not freed until the block it was retired in ends",
          "[device][rt]") {
    // The rule the whole scheme rests on. A buffer displaced during block N may
    // have been loaded by block N a moment earlier and not yet handed to a
    // voice, so it cannot be freed while the counter still reads N.
    Device device;
    device.prepare(44100.0, 64);

    device.setSlotSample(0, makeBuffer(1000, 1.0f));
    const auto blockAtRetirement = device.blockCount();

    device.setSlotSample(0, makeBuffer(1000, 2.0f));
    REQUIRE(device.pendingRetiredSamples() == 1);

    // Counter has not moved: nothing may be freed.
    CHECK(device.collectRetiredSamples() == 0);
    CHECK(device.pendingRetiredSamples() == 1);

    runBlock(device);
    REQUIRE(device.blockCount() != blockAtRetirement);

    CHECK(device.collectRetiredSamples() == 1);
    CHECK(device.pendingRetiredSamples() == 0);
}

TEST_CASE("A buffer a voice is still playing is not freed", "[device][rt]") {
    // The case a purely time-based rule gets wrong: a note can outlive any
    // number of blocks, so the hazard set has to be consulted too.
    Device device;
    device.prepare(44100.0, 64);

    // Long enough that the voice cannot run out during the test.
    device.setSlotSample(0, makeBuffer(44100, 1.0f));
    device.pad(0, 0).slotIndex = 0;

    push(device, Command::Type::NoteOn, 0);
    runBlock(device);
    REQUIRE(device.voices().activeVoiceCount() == 1);

    // Swap the sample out from under the playing voice.
    device.setSlotSample(0, makeBuffer(44100, 2.0f));
    REQUIRE(device.pendingRetiredSamples() == 1);

    // Blocks pass, but the voice is still reading the old buffer.
    for (int i = 0; i < 5; ++i) {
        runBlock(device);
        CHECK(device.collectRetiredSamples() == 0);
    }
    CHECK(device.pendingRetiredSamples() == 1);

    // Once nothing is playing it, it goes.
    push(device, Command::Type::AllNotesOff);
    runBlock(device);
    REQUIRE(device.voices().activeVoiceCount() == 0);

    CHECK(device.collectRetiredSamples() == 1);
    CHECK(device.pendingRetiredSamples() == 0);
}

TEST_CASE("A voice keeps playing the buffer it started on", "[device][rt]") {
    // Swapping a slot must not yank the audio out from under a sounding note.
    // The pointer the voice captured stays valid and stays the one it plays.
    Device device;
    device.prepare(44100.0, 64);

    device.setSlotSample(0, makeBuffer(44100, 1.0f));
    device.pad(0, 0).slotIndex = 0;
    device.pad(0, 0).level     = 1.0f;

    push(device, Command::Type::NoteOn, 0);
    runBlock(device);

    device.setSlotSample(0, makeBuffer(44100, 9.0f));

    std::vector<float> left(64, 0.0f), right(64, 0.0f);
    const seq::TransportInfo transport;
    device.processBlock(left.data(), right.data(), 64, transport);

    // Still the original 1.0 material, not the 9.0 replacement. The lo-fi
    // stage quantises the level, so this is a range check rather than equality.
    CHECK(left[0] > 0.5f);
    CHECK(left[0] < 2.0f);
}

TEST_CASE("Repeated loads do not accumulate retired buffers", "[device][rt]") {
    // Dropping a folder of samples onto a pad must not grow memory without
    // bound; each load should free the one before it on the next block.
    Device device;
    device.prepare(44100.0, 64);

    for (int i = 0; i < 20; ++i) {
        device.setSlotSample(0, makeBuffer(500, static_cast<float>(i)));
        runBlock(device);
        device.collectRetiredSamples();
    }

    CHECK(device.pendingRetiredSamples() <= 1);
}

TEST_CASE("Collecting with nothing retired is a no-op", "[device][rt]") {
    Device device;
    device.prepare(44100.0, 64);

    CHECK(device.collectRetiredSamples() == 0);
    CHECK(device.pendingRetiredSamples() == 0);
}
