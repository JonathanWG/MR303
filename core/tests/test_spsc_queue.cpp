#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "sp303/rt/SpscQueue.h"

using namespace sp303::rt;

TEST_CASE("SpscQueue round-trips values in order", "[rt][queue]") {
    SpscQueue<int> q(16);

    REQUIRE(q.empty());
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));
    REQUIRE_FALSE(q.empty());

    int v = 0;
    REQUIRE(q.pop(v)); CHECK(v == 1);
    REQUIRE(q.pop(v)); CHECK(v == 2);
    REQUIRE(q.pop(v)); CHECK(v == 3);
    CHECK(q.empty());
    CHECK_FALSE(q.pop(v));
}

TEST_CASE("SpscQueue reports full rather than overwriting", "[rt][queue]") {
    // Silently dropping the oldest command would mean a NoteOff could vanish
    // and leave a voice stuck on. The caller must see the failure.
    SpscQueue<int> q(4);

    int pushed = 0;
    while (q.push(pushed)) ++pushed;

    CHECK(pushed > 0);
    CHECK(pushed == static_cast<int>(q.capacity()));

    int v = 0;
    REQUIRE(q.pop(v));
    CHECK(v == 0);
    CHECK(q.push(999));
}

TEST_CASE("SpscQueue capacity rounds up to a power of two", "[rt][queue]") {
    CHECK(SpscQueue<int>(5).capacity()  == 7);   // 8  - 1
    CHECK(SpscQueue<int>(16).capacity() == 15);  // 16 - 1
    CHECK(SpscQueue<int>(17).capacity() == 31);  // 32 - 1
}

TEST_CASE("SpscQueue survives concurrent producer and consumer", "[rt][queue]") {
    constexpr int kCount = 100000;
    SpscQueue<int> q(1024);

    std::atomic<bool> producerDone {false};
    std::vector<int>  received;
    received.reserve(kCount);

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i)
            while (!q.push(i)) std::this_thread::yield();
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        int v = 0;
        while (received.size() < kCount) {
            if (q.pop(v)) {
                received.push_back(v);
            } else if (producerDone.load(std::memory_order_acquire) && q.empty()) {
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(received.size() == kCount);
    for (int i = 0; i < kCount; ++i)
        REQUIRE(received[static_cast<std::size_t>(i)] == i);
}
