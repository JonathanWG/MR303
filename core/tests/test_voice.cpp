#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "sp303/SampleBuffer.h"
#include "sp303/voice/Voice.h"
#include "sp303/voice/VoiceManager.h"

using namespace sp303;
using Catch::Approx;

namespace {

// Mono buffer of ascending values, so a voice's read position is directly
// readable from its output.
SampleBufferPtr makeRamp(int frames, double rate = 44100.0) {
    std::vector<std::vector<float>> channels(1);
    channels[0].resize(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i)
        channels[0][static_cast<std::size_t>(i)] = static_cast<float>(i);
    return std::make_shared<const SampleBuffer>(std::move(channels), rate, "ramp");
}

Pad makePad(int slot = 0) {
    Pad p;
    p.slotIndex = slot;
    p.level     = 1.0f;
    return p;
}

}  // namespace

TEST_CASE("A one-shot voice stops at the end of the sample", "[voice]") {
    Voice v;
    v.prepare(44100.0);
    v.setInterpolationMode(dsp::InterpolationMode::DropSample);

    auto buffer = makeRamp(10);
    v.start(makePad(), buffer.get(), 0);
    REQUIRE(v.isActive());

    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 10; ++i) v.renderNextFrame(l, r);

    CHECK_FALSE(v.isActive());
}

TEST_CASE("A looping voice keeps playing past the end", "[voice]") {
    Voice v;
    v.prepare(44100.0);
    v.setInterpolationMode(dsp::InterpolationMode::DropSample);

    auto pad = makePad();
    pad.loop = LoopMode::Loop;

    auto buffer = makeRamp(10);
    v.start(pad, buffer.get(), 0);

    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 100; ++i) v.renderNextFrame(l, r);

    CHECK(v.isActive());
}

TEST_CASE("Gate mode stops on release, Trigger mode ignores it", "[voice]") {
    auto buffer = makeRamp(1000);

    SECTION("gate") {
        Voice v;
        v.prepare(44100.0);
        auto pad = makePad();
        pad.trigger = TriggerMode::Gate;

        v.start(pad, buffer.get(), 0);
        REQUIRE(v.isActive());
        v.release();
        CHECK_FALSE(v.isActive());
    }

    SECTION("trigger") {
        Voice v;
        v.prepare(44100.0);
        auto pad = makePad();
        pad.trigger = TriggerMode::Trigger;

        v.start(pad, buffer.get(), 0);
        v.release();
        CHECK(v.isActive());
    }

    SECTION("gate with HOLD latches") {
        Voice v;
        v.prepare(44100.0);
        auto pad = makePad();
        pad.trigger = TriggerMode::Gate;
        pad.hold    = true;

        v.start(pad, buffer.get(), 0);
        v.release();
        CHECK(v.isActive());
    }
}

TEST_CASE("Reverse playback walks backwards through the sample", "[voice]") {
    Voice v;
    v.prepare(44100.0);
    v.setInterpolationMode(dsp::InterpolationMode::DropSample);

    auto pad = makePad();
    pad.reverse = true;
    auto buffer = makeRamp(10);
    v.start(pad, buffer.get(), 0);

    float l = 0.0f, r = 0.0f;
    v.renderNextFrame(l, r);

    // The ramp holds its index as its value, so the first reverse frame must
    // be near the end of the buffer.
    CHECK(l == Approx(9.0f));
}

TEST_CASE("START and END restrict playback to a region", "[voice]") {
    Voice v;
    v.prepare(44100.0);
    v.setInterpolationMode(dsp::InterpolationMode::DropSample);

    auto pad = makePad();
    pad.startFrame = 5;
    pad.endFrame   = 8;
    auto buffer = makeRamp(20);
    v.start(pad, buffer.get(), 0);

    float l = 0.0f, r = 0.0f;
    v.renderNextFrame(l, r);
    CHECK(l == Approx(5.0f));

    // Three frames of region: 5, 6, 7 - then it must stop.
    l = 0.0f;
    v.renderNextFrame(l, r);
    v.renderNextFrame(l, r);
    CHECK_FALSE(v.isActive());
}

TEST_CASE("VoiceManager respects the 8-voice polyphony limit", "[voice][manager]") {
    VoiceManager mgr;
    mgr.prepare(44100.0);

    auto buffer = makeRamp(44100);
    for (int i = 0; i < kNumPads; ++i)
        mgr.noteOn(i, makePad(i), buffer.get());

    CHECK(mgr.activeVoiceCount() == kMaxVoices);
}

TEST_CASE("Retriggering the same pad restarts rather than layering", "[voice][manager]") {
    // This is what makes fast stutter rolls work the way players expect.
    VoiceManager mgr;
    mgr.prepare(44100.0);

    auto buffer = makeRamp(44100);
    mgr.noteOn(0, makePad(0), buffer.get());
    mgr.noteOn(0, makePad(0), buffer.get());
    mgr.noteOn(0, makePad(0), buffer.get());

    CHECK(mgr.activeVoiceCount() == 1);
}

TEST_CASE("An empty pad produces no voice", "[voice][manager]") {
    VoiceManager mgr;
    mgr.prepare(44100.0);

    Pad empty;  // slotIndex == -1
    auto buffer = makeRamp(100);
    mgr.noteOn(0, empty, buffer.get());

    CHECK(mgr.activeVoiceCount() == 0);
}

TEST_CASE("VoiceManager accumulates into the output buffers", "[voice][manager]") {
    // render() must add, not overwrite - the caller owns the mix bus.
    VoiceManager mgr;
    mgr.prepare(44100.0);
    mgr.setInterpolationMode(dsp::InterpolationMode::DropSample);

    std::vector<float> left(16, 1.0f), right(16, 1.0f);
    mgr.render(left.data(), right.data(), 16);

    CHECK(left[0] == Approx(1.0f));  // no voices -> untouched

    auto ramp = makeRamp(64);
    mgr.noteOn(0, makePad(0), ramp.get());
    mgr.render(left.data(), right.data(), 16);

    CHECK(left[1] > 1.0f);  // ramp value added on top of the existing 1.0
}
