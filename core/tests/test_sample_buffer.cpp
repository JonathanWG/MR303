#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <vector>

#include "sp303/SampleBuffer.h"
#include "sp303/rt/HazardPointers.h"

using namespace sp303;
using Catch::Approx;

TEST_CASE("An empty buffer reports itself as empty", "[sample]") {
    SampleBuffer buffer;
    CHECK(buffer.isEmpty());
    CHECK(buffer.numFrames() == 0);
    CHECK(buffer.channel(0) == nullptr);
}

TEST_CASE("Ragged channels are padded to equal length", "[sample]") {
    // Otherwise the audio thread reads past the end of the shorter channel.
    std::vector<std::vector<float>> channels {
        std::vector<float>(100, 1.0f),
        std::vector<float>(50,  1.0f)
    };
    SampleBuffer buffer(std::move(channels), 44100.0, "ragged");

    REQUIRE(buffer.numChannels() == 2);
    REQUIRE(buffer.numFrames() == 100);

    const float* right = buffer.channel(1);
    REQUIRE(right != nullptr);
    CHECK(right[49] == Approx(1.0f));
    CHECK(right[99] == Approx(0.0f));  // padded
}

TEST_CASE("Out-of-range channel access returns nullptr", "[sample]") {
    std::vector<std::vector<float>> channels {std::vector<float>(10, 0.5f)};
    SampleBuffer buffer(std::move(channels), 44100.0, "mono");

    CHECK(buffer.channel(0)  != nullptr);
    CHECK(buffer.channel(1)  == nullptr);
    CHECK(buffer.channel(-1) == nullptr);
}

TEST_CASE("BPM is derived from duration, not from beat detection", "[sample]") {
    // Two seconds of audio holding one bar of 4/4 is 120 BPM.
    std::vector<std::vector<float>> channels {std::vector<float>(88200, 0.0f)};
    SampleBuffer buffer(std::move(channels), 44100.0, "twoSeconds");

    CHECK(buffer.estimateBpm(1, 4) == Approx(120.0));
    CHECK(buffer.estimateBpm(2, 4) == Approx(240.0));
}

TEST_CASE("BPM estimation rejects degenerate input", "[sample]") {
    SampleBuffer empty;
    CHECK(empty.estimateBpm(1, 4) == Approx(0.0));

    std::vector<std::vector<float>> channels {std::vector<float>(1000, 0.0f)};
    SampleBuffer buffer(std::move(channels), 44100.0, "x");
    CHECK(buffer.estimateBpm(0, 4) == Approx(0.0));
    CHECK(buffer.estimateBpm(1, 0) == Approx(0.0));
}

TEST_CASE("SampleSlot publishes a pointer the audio thread can read",
          "[sample][rt]") {
    SampleSlot slot;
    CHECK(slot.isEmpty());
    CHECK(slot.load() == nullptr);

    std::vector<std::vector<float>> first {std::vector<float>(10, 1.0f)};
    auto displaced = slot.publish(
        std::make_shared<const SampleBuffer>(std::move(first), 44100.0, "first"));

    CHECK(displaced == nullptr);  // nothing was there
    CHECK_FALSE(slot.isEmpty());

    const SampleBuffer* held = slot.load();
    REQUIRE(held != nullptr);
    CHECK(held->channel(0)[0] == Approx(1.0f));
    CHECK(slot.owned().get() == held);
}

TEST_CASE("SampleSlot hands back what it displaced instead of freeing it",
          "[sample][rt]") {
    // This is the whole safety contract. The audio thread may be holding a raw
    // pointer to the old buffer right now, so publish() must not be the thing
    // that destroys it - it gives it back and lets the caller retire it.
    SampleSlot slot;

    std::vector<std::vector<float>> first {std::vector<float>(10, 1.0f)};
    auto nothing = slot.publish(
        std::make_shared<const SampleBuffer>(std::move(first), 44100.0, "first"));
    REQUIRE(nothing == nullptr);

    const SampleBuffer* older = slot.load();

    std::vector<std::vector<float>> second {std::vector<float>(10, 2.0f)};
    auto displaced = slot.publish(
        std::make_shared<const SampleBuffer>(std::move(second), 44100.0, "second"));

    REQUIRE(displaced != nullptr);
    CHECK(displaced.get() == older);              // exactly what was there
    CHECK(displaced->channel(0)[0] == Approx(1.0f));   // still intact
    CHECK(slot.load()->channel(0)[0] == Approx(2.0f)); // and replaced

    // While the caller holds `displaced`, the old pointer stays dereferenceable.
    CHECK(older->channel(0)[0] == Approx(1.0f));
}

TEST_CASE("SampleSlot's published pointer is lock-free to read", "[sample][rt]") {
    // The reason this class stopped using std::atomic<std::shared_ptr>. That
    // one measured NOT lock-free on MSVC/x64, which put a mutex on the audio
    // thread. A raw pointer atomic is lock-free everywhere this ships, so this
    // is a hard assertion rather than the WARN it used to be.
    std::atomic<const SampleBuffer*> probe {nullptr};
    CHECK(probe.is_lock_free());
    CHECK(rt::HazardPointers<8>::isLockFree());
}

TEST_CASE("SampleSlot survives concurrent swap and read", "[sample][rt]") {
    SampleSlot slot;

    // The reader holds raw pointers, so the writer keeps every buffer it has
    // ever published alive for the duration. That is exactly what Device's
    // retirement list does in production, modelled here in the simplest way
    // that makes the race meaningful.
    std::vector<SampleBufferPtr> keepAlive;

    std::vector<std::vector<float>> initial {std::vector<float>(64, 1.0f)};
    keepAlive.push_back(std::make_shared<const SampleBuffer>(
        std::move(initial), 44100.0, "init"));
    (void)slot.publish(keepAlive.back());

    std::atomic<bool> stop {false};

    std::thread writer([&] {
        for (int i = 0; i < 2000; ++i) {
            std::vector<std::vector<float>> data {
                std::vector<float>(64, static_cast<float>(i))
            };
            auto next = std::make_shared<const SampleBuffer>(
                std::move(data), 44100.0, "swap");
            keepAlive.push_back(next);
            auto displaced = slot.publish(std::move(next));
            keepAlive.push_back(std::move(displaced));
        }
        stop.store(true);
    });

    std::thread reader([&] {
        while (!stop.load()) {
            const SampleBuffer* buffer = slot.load();
            if (buffer != nullptr && !buffer->isEmpty()) {
                const float* d = buffer->channel(0);
                REQUIRE(d != nullptr);
                // Every frame in a given buffer holds the same value; a torn
                // read would show a mismatch.
                REQUIRE(d[0] == Approx(d[63]));
            }
        }
    });

    writer.join();
    reader.join();
    SUCCEED();
}
