#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "sp303/dsp/DelayLine.h"
#include "sp303/fx/Delay.h"
#include "sp303/fx/Isolator.h"
#include "sp303/fx/Pitch.h"
#include "sp303/fx/VinylSim.h"

using namespace sp303;
using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;

    explicit Stereo(int n)
        : left(static_cast<std::size_t>(n), 0.0f),
          right(static_cast<std::size_t>(n), 0.0f) {}

    int size() const noexcept { return static_cast<int>(left.size()); }
};

Stereo sine(int numFrames, double frequency, double sampleRate,
            float amplitude = 0.5f) {
    Stereo s(numFrames);
    for (int n = 0; n < numFrames; ++n) {
        const auto v = static_cast<float>(
            amplitude * std::sin(2.0 * kPi * frequency * n / sampleRate));
        s.left[static_cast<std::size_t>(n)]  = v;
        s.right[static_cast<std::size_t>(n)] = v;
    }
    return s;
}

Stereo constant(int numFrames, float value) {
    Stereo s(numFrames);
    for (int n = 0; n < numFrames; ++n) {
        s.left[static_cast<std::size_t>(n)]  = value;
        s.right[static_cast<std::size_t>(n)] = value;
    }
    return s;
}

// Runs the whole signal through in one block. Effects are block-size agnostic
// by contract, and a single block keeps the tests reading like maths.
void render(fx::IEffect& effect, Stereo& audio, double sampleRate,
            float c1, float c2, float c3) {
    effect.prepare(sampleRate, audio.size());
    effect.setControls(c1, c2, c3);
    effect.reset();  // snaps any glide to its target, so timing is exact
    effect.process(audio.left.data(), audio.right.data(), audio.size());
}

double rms(const std::vector<float>& data, int from = 0) {
    double sum = 0.0;
    int count = 0;
    for (auto i = static_cast<std::size_t>(from); i < data.size(); ++i) {
        sum += static_cast<double>(data[i]) * data[i];
        ++count;
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

bool allFinite(const Stereo& audio) {
    for (int n = 0; n < audio.size(); ++n) {
        if (!std::isfinite(audio.left[static_cast<std::size_t>(n)])) return false;
        if (!std::isfinite(audio.right[static_cast<std::size_t>(n)])) return false;
    }
    return true;
}

float peak(const Stereo& audio) {
    float p = 0.0f;
    for (int n = 0; n < audio.size(); ++n) {
        p = std::max(p, std::abs(audio.left[static_cast<std::size_t>(n)]));
        p = std::max(p, std::abs(audio.right[static_cast<std::size_t>(n)]));
    }
    return p;
}

}  // namespace

// ===========================================================================
// DelayLine - the shared primitive under Delay, Pitch and Vinyl Sim.
// ===========================================================================

TEST_CASE("DelayLine reads back samples by age", "[dsp][delayline]") {
    dsp::DelayLine line;
    line.prepare(10);

    for (int i = 0; i < 6; ++i)
        line.write(static_cast<float>(i));  // 0,1,2,3,4,5

    CHECK(line.read(0.0) == Approx(5.0f));  // most recent
    CHECK(line.read(1.0) == Approx(4.0f));
    CHECK(line.read(5.0) == Approx(0.0f));
}

TEST_CASE("DelayLine interpolates between integer taps", "[dsp][delayline]") {
    // Continuous interpolation is what lets the delay time be swept without
    // stepping, so it is the property worth pinning down.
    dsp::DelayLine line;
    line.prepare(10);

    line.write(0.0f);
    line.write(10.0f);

    CHECK(line.read(0.5) == Approx(5.0f));   // halfway between 10 and 0
    CHECK(line.read(0.25) == Approx(7.5f));
}

TEST_CASE("DelayLine clamps rather than wrapping onto fresh audio",
          "[dsp][delayline]") {
    // Reading further back than the line holds must not alias onto recent
    // samples - that would sound like a broken feedback loop, not like a bug.
    dsp::DelayLine line;
    line.prepare(4);

    for (int i = 1; i <= 4; ++i)
        line.write(static_cast<float>(i));

    const float far = line.read(1000.0);
    CHECK(far != Approx(4.0f));  // not the newest sample
    CHECK(std::isfinite(far));
}

TEST_CASE("DelayLine reset clears history", "[dsp][delayline]") {
    dsp::DelayLine line;
    line.prepare(8);

    for (int i = 0; i < 8; ++i) line.write(1.0f);
    REQUIRE(line.read(3.0) == Approx(1.0f));

    line.reset();
    CHECK(line.read(0.0) == Approx(0.0f));
    CHECK(line.read(3.0) == Approx(0.0f));
}

TEST_CASE("An unprepared DelayLine is inert, not a crash", "[dsp][delayline]") {
    dsp::DelayLine line;
    line.write(1.0f);
    CHECK(line.read(0.0) == Approx(0.0f));
    CHECK(line.capacity() == 0);
}

// ===========================================================================
// Delay
// ===========================================================================

TEST_CASE("Delay at LEVEL 0 passes the dry signal untouched", "[fx][delay]") {
    // Not "approximately": the wet path is multiplied by level, so zero level
    // must be bit-identical to the input. A colouring bypass is a bug users
    // discover only by null-testing.
    constexpr double rate = 1000.0;
    auto audio = sine(500, 50.0, rate);
    const auto dry = audio.left;

    fx::Delay delay;
    render(delay, audio, rate, 0.5f, 0.5f, 0.0f);

    for (std::size_t i = 0; i < dry.size(); ++i)
        REQUIRE(audio.left[i] == dry[i]);
}

TEST_CASE("Delay repeats an impulse one delay time later", "[fx][delay]") {
    constexpr double rate = 1000.0;

    // CTRL1 = 0 maps to the minimum, 20 ms, which at 1 kHz is 20 samples.
    constexpr int expected = 20;

    Stereo audio(200);
    audio.left[0] = audio.right[0] = 1.0f;

    fx::Delay delay;
    render(delay, audio, rate, 0.0f, 0.0f, 1.0f);

    int loudest = -1;
    float best = 0.0f;
    for (int n = 1; n < audio.size(); ++n) {
        const float a = std::abs(audio.left[static_cast<std::size_t>(n)]);
        if (a > best) { best = a; loudest = n; }
    }

    // read-before-write inside the loop puts the echo one sample past the
    // nominal time; anything further out means the mapping is wrong.
    CHECK(loudest >= expected);
    CHECK(loudest <= expected + 1);
    CHECK(best > 0.5f);
}

TEST_CASE("Delay repeats decay and never grow", "[fx][delay]") {
    // The feedback cap is what stops this becoming a speaker-damaging bug, so
    // it is checked at the maximum setting rather than a comfortable one.
    constexpr double rate = 1000.0;

    Stereo audio(4000);
    audio.left[0] = audio.right[0] = 1.0f;

    fx::Delay delay;
    render(delay, audio, rate, 0.0f, 1.0f, 1.0f);

    REQUIRE(allFinite(audio));

    // Compare the first second of repeats against the last: energy must fall.
    const double early = rms(std::vector<float>(audio.left.begin(),
                                                audio.left.begin() + 1000));
    const double late  = rms(std::vector<float>(audio.left.end() - 1000,
                                                audio.left.end()));
    CHECK(late < early);
}

TEST_CASE("Delay produces silence from silence", "[fx][delay]") {
    constexpr double rate = 1000.0;
    Stereo audio(2000);

    fx::Delay delay;
    render(delay, audio, rate, 0.5f, 1.0f, 1.0f);

    CHECK(peak(audio) == Approx(0.0f));
}

// ===========================================================================
// Pitch
// ===========================================================================

TEST_CASE("Pitch at MIX 0 passes the dry signal untouched", "[fx][pitch]") {
    constexpr double rate = 1000.0;
    auto audio = sine(500, 50.0, rate);
    const auto dry = audio.left;

    fx::Pitch pitch;
    render(pitch, audio, rate, 0.2f, 0.5f, 0.0f);

    for (std::size_t i = 0; i < dry.size(); ++i)
        REQUIRE(audio.left[i] == dry[i]);
}

TEST_CASE("Pitch crossfade is gain-neutral at every shift", "[fx][pitch]") {
    // The two read heads carry complementary Hann windows, so their gains sum
    // to exactly 1. If that ever breaks, the output pumps at the window rate -
    // an artefact that is easy to introduce and hard to hear as a bug rather
    // than as "character". DC in, DC out is the sharpest way to catch it.
    constexpr double rate = 1000.0;

    const float shifts[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float grains[] = {0.0f, 0.5f, 1.0f};

    for (float shift : shifts) {
        for (float grain : grains) {
            auto audio = constant(2000, 1.0f);

            fx::Pitch pitch;
            render(pitch, audio, rate, shift, grain, 1.0f);

            REQUIRE(allFinite(audio));

            // Skip the head, where the delay line is still filling with zeros.
            const double level = rms(audio.left, 1000);
            INFO("shift " << shift << " grain " << grain);
            CHECK(level == Approx(1.0).margin(0.02));
        }
    }
}

TEST_CASE("Pitch stays bounded across its whole range", "[fx][pitch]") {
    constexpr double rate = 44100.0;

    for (float shift = 0.0f; shift <= 1.0f; shift += 0.1f) {
        auto audio = sine(8000, 220.0, rate);

        fx::Pitch pitch;
        render(pitch, audio, rate, shift, 0.5f, 1.0f);

        INFO("shift " << shift);
        REQUIRE(allFinite(audio));
        CHECK(peak(audio) < 2.0f);
    }
}

// ===========================================================================
// Vinyl Sim
// ===========================================================================

TEST_CASE("Vinyl Sim adds nothing to silence with NOISE at zero",
          "[fx][vinyl]") {
    constexpr double rate = 44100.0;
    Stereo audio(4000);

    fx::VinylSim vinyl;
    render(vinyl, audio, rate, 0.0f, 1.0f, 1.0f);

    CHECK(peak(audio) == Approx(0.0f));
}

TEST_CASE("Vinyl Sim noise is deterministic", "[fx][vinyl]") {
    // The RNG is a seeded xorshift rather than std::random precisely so that
    // rendering a project twice produces the same audio, and so golden-file
    // regressions are stable. Two instances must agree sample for sample.
    constexpr double rate = 44100.0;

    Stereo first(4000), second(4000);

    fx::VinylSim a, b;
    render(a, first,  rate, 1.0f, 0.0f, 0.0f);
    render(b, second, rate, 1.0f, 0.0f, 0.0f);

    REQUIRE(peak(first) > 0.0f);  // there is actually noise to compare

    for (int n = 0; n < first.size(); ++n) {
        REQUIRE(first.left[static_cast<std::size_t>(n)]
                == second.left[static_cast<std::size_t>(n)]);
    }
}

TEST_CASE("Vinyl Sim noise is not identical in both channels", "[fx][vinyl]") {
    // Identical noise in L and R collapses to a mono hiss pinned dead centre,
    // which reads as synthetic next to the audio. Each channel is seeded apart.
    constexpr double rate = 44100.0;
    Stereo audio(4000);

    fx::VinylSim vinyl;
    render(vinyl, audio, rate, 1.0f, 0.0f, 0.0f);

    bool differs = false;
    for (int n = 0; n < audio.size() && !differs; ++n) {
        differs = audio.left[static_cast<std::size_t>(n)]
               != audio.right[static_cast<std::size_t>(n)];
    }
    CHECK(differs);
}

TEST_CASE("Vinyl Sim TONE narrows the bandwidth", "[fx][vinyl]") {
    constexpr double rate = 44100.0;

    auto open   = sine(8000, 10000.0, rate);
    auto closed = sine(8000, 10000.0, rate);

    fx::VinylSim a, b;
    render(a, open,   rate, 0.0f, 0.0f, 0.0f);  // TONE 0 - wide
    render(b, closed, rate, 0.0f, 0.0f, 1.0f);  // TONE 1 - narrow

    // A 10 kHz tone sits above the closed-down lowpass and must lose energy.
    CHECK(rms(closed.left, 2000) < rms(open.left, 2000) * 0.5);
}

TEST_CASE("Vinyl Sim stays bounded with everything up", "[fx][vinyl]") {
    constexpr double rate = 44100.0;
    auto audio = sine(8000, 220.0, rate);

    fx::VinylSim vinyl;
    render(vinyl, audio, rate, 1.0f, 1.0f, 1.0f);

    REQUIRE(allFinite(audio));
    CHECK(peak(audio) < 2.0f);
}

// ===========================================================================
// Isolator
// ===========================================================================

TEST_CASE("Isolator is flat in magnitude with all bands centred",
          "[fx][isolator]") {
    // This is the property LR4 crossovers are chosen for. Without it,
    // "everything at noon" would already colour the signal and every
    // performance move would start from a lie.
    //
    // Flat in MAGNITUDE, not in phase: an LR4 split sums to an allpass, so the
    // output is not sample-identical to the input. Comparing waveforms here
    // would measure the wrong thing and fail for the wrong reason.
    constexpr double rate = 44100.0;

    const double frequencies[] = {50.0, 100.0, 200.0, 1000.0,
                                  4000.0, 8000.0, 16000.0};

    for (double frequency : frequencies) {
        auto audio = sine(16000, frequency, rate);
        const auto dry = audio.left;

        fx::Isolator isolator;
        render(isolator, audio, rate, 0.5f, 0.5f, 0.5f);

        // Skip the head while the filters settle.
        const double out = rms(audio.left, 4000);
        const double in  = rms(dry, 4000);

        INFO(frequency << " Hz");
        CHECK(out == Approx(in).epsilon(0.05));
    }
}

TEST_CASE("Isolator takes a band to actual silence", "[fx][isolator]") {
    // A kill EQ that only reaches -12 dB is a tone control. The bottom of the
    // knob has to mean off.
    constexpr double rate = 44100.0;
    auto audio = sine(4000, 1000.0, rate);

    fx::Isolator isolator;
    render(isolator, audio, rate, 0.0f, 0.0f, 0.0f);

    CHECK(peak(audio) == Approx(0.0f));
}

TEST_CASE("Isolator bands select the frequencies they name", "[fx][isolator]") {
    constexpr double rate = 44100.0;

    SECTION("LOW alone passes bass and rejects treble") {
        auto bass    = sine(16000, 60.0, rate);
        auto treble  = sine(16000, 10000.0, rate);

        fx::Isolator a, b;
        render(a, bass,   rate, 0.5f, 0.0f, 0.0f);
        render(b, treble, rate, 0.5f, 0.0f, 0.0f);

        CHECK(rms(bass.left, 4000) > 0.2);
        CHECK(rms(treble.left, 4000) < 0.01);
    }

    SECTION("HIGH alone passes treble and rejects bass") {
        auto bass   = sine(16000, 60.0, rate);
        auto treble = sine(16000, 10000.0, rate);

        fx::Isolator a, b;
        render(a, bass,   rate, 0.0f, 0.0f, 0.5f);
        render(b, treble, rate, 0.0f, 0.0f, 0.5f);

        CHECK(rms(bass.left, 4000) < 0.01);
        CHECK(rms(treble.left, 4000) > 0.2);
    }
}

TEST_CASE("Isolator centre is unity, not merely close", "[fx][isolator]") {
    // 0.5 has to be a real detent: a band left at noon must neither cut nor
    // boost, or every knob becomes a mix decision.
    constexpr double rate = 44100.0;
    auto audio = sine(16000, 1000.0, rate);
    const auto dry = audio.left;

    fx::Isolator isolator;
    render(isolator, audio, rate, 0.5f, 0.5f, 0.5f);

    CHECK(rms(audio.left, 4000) == Approx(rms(dry, 4000)).epsilon(0.02));
}
