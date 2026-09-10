#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "sp303/dsp/Lfo.h"
#include "sp303/fx/Chorus.h"
#include "sp303/fx/EffectRack.h"
#include "sp303/fx/Flanger.h"
#include "sp303/fx/Phaser.h"
#include "sp303/fx/Reverb.h"
#include "sp303/fx/TapeEcho.h"

using namespace sp303;
using Catch::Approx;

// The five MFX written on 05/09/2026 - Reverb, Tape Echo, Chorus, Flanger,
// Phaser - and the dsp::Lfo three of them share. Same conventions as
// test_effects.cpp: a whole signal in one block, low sample rates where the
// arithmetic then reads as maths, and properties that hold by construction
// (exact bypass, silence in / silence out, bounded with everything up,
// analytic notch and peak positions) rather than golden waveforms. None of
// this says the effects sound like the hardware - see docs/HARDWARE_FACTS.md.

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
    std::fill(s.left.begin(),  s.left.end(),  value);
    std::fill(s.right.begin(), s.right.end(), value);
    return s;
}

Stereo impulse(int numFrames) {
    Stereo s(numFrames);
    s.left[0] = s.right[0] = 1.0f;
    return s;
}

void render(fx::IEffect& effect, Stereo& audio, double sampleRate,
            float c1, float c2, float c3) {
    effect.prepare(sampleRate, audio.size());
    effect.setControls(c1, c2, c3);
    effect.reset();
    effect.process(audio.left.data(), audio.right.data(), audio.size());
}

double rms(const std::vector<float>& data, int from = 0, int to = -1) {
    const auto end = to < 0 ? data.size() : static_cast<std::size_t>(to);
    double sum = 0.0;
    int count = 0;
    for (auto i = static_cast<std::size_t>(from); i < end; ++i) {
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

bool channelsDiffer(const Stereo& audio) {
    for (int n = 0; n < audio.size(); ++n) {
        if (audio.left[static_cast<std::size_t>(n)]
            != audio.right[static_cast<std::size_t>(n)]) return true;
    }
    return false;
}

// Index of the loudest sample strictly after `from`.
int loudestAfter(const std::vector<float>& data, int from) {
    int   best  = -1;
    float value = 0.0f;
    for (auto n = static_cast<std::size_t>(from + 1); n < data.size(); ++n) {
        if (std::abs(data[n]) > value) {
            value = std::abs(data[n]);
            best  = static_cast<int>(n);
        }
    }
    return best;
}

}  // namespace

// ===========================================================================
// Lfo - the modulator under Chorus, Flanger, Phaser and Tape Echo.
// ===========================================================================

TEST_CASE("Lfo phase is kept in cycles and wraps", "[dsp][lfo]") {
    dsp::Lfo lfo;
    lfo.prepare(4.0);
    lfo.setRate(1.0);  // one cycle every four samples

    CHECK(lfo.phase() == Approx(0.0));
    lfo.advance();
    CHECK(lfo.phase() == Approx(0.25));
    lfo.advance();
    lfo.advance();
    lfo.advance();
    CHECK(lfo.phase() == Approx(0.0).margin(1e-12));  // wrapped, not 1.0
}

TEST_CASE("Lfo waveforms have the documented shape", "[dsp][lfo]") {
    dsp::Lfo lfo;
    lfo.prepare(1000.0);

    SECTION("sine starts at zero and peaks a quarter cycle on") {
        lfo.reset(0.0);
        CHECK(lfo.sine() == Approx(0.0).margin(1e-12));
        CHECK(lfo.sine(0.25) == Approx(1.0));
        lfo.reset(0.25);
        CHECK(lfo.sine() == Approx(1.0));
    }

    SECTION("triangle is -1 at phase 0, +1 at 0.5, and linear between") {
        lfo.reset(0.0);
        CHECK(lfo.triangle() == Approx(-1.0));
        CHECK(lfo.triangle(0.25) == Approx(0.0).margin(1e-12));
        CHECK(lfo.triangle(0.5) == Approx(1.0));
        CHECK(lfo.triangle(0.75) == Approx(0.0).margin(1e-12));
        lfo.reset(0.5);
        CHECK(lfo.triangle() == Approx(1.0));
        // An offset past the end of the cycle wraps rather than escaping [-1, 1].
        CHECK(lfo.triangle(0.5) == Approx(-1.0));
    }
}

TEST_CASE("Lfo stereo readings hold their relationship", "[dsp][lfo]") {
    // This is why the effects read ONE oscillator at two offsets instead of
    // running two: the offset is exact at every sample, forever.
    dsp::Lfo lfo;
    lfo.prepare(1000.0);
    lfo.setRate(3.7);

    for (int n = 0; n < 5000; ++n) {
        lfo.advance();
        const double a = lfo.sine();
        const double b = lfo.sine(0.25);
        // sin(x)^2 + sin(x + pi/2)^2 == 1
        REQUIRE(a * a + b * b == Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("Lfo rejects a negative rate and stands still at zero", "[dsp][lfo]") {
    dsp::Lfo lfo;
    lfo.prepare(1000.0);

    lfo.setRate(-5.0);
    CHECK(lfo.rate() == Approx(0.0));

    lfo.reset(0.3);
    for (int n = 0; n < 100; ++n) lfo.advance();
    CHECK(lfo.phase() == Approx(0.3));
}

// ===========================================================================
// EffectRack knows about the new bank.
// ===========================================================================

TEST_CASE("EffectRack instantiates the five MFX written so far", "[fx][rack]") {
    fx::EffectRack rack;

    const EffectId written[] = {EffectId::Reverb, EffectId::TapeEcho,
                                EffectId::Chorus, EffectId::Flanger,
                                EffectId::Phaser};
    for (EffectId id : written) {
        INFO(fx::effectName(id));
        CHECK(rack.isAvailable(id));
    }

    // Still honest about what does not exist.
    const EffectId missing[] = {EffectId::Slicer, EffectId::VoiceTransformer,
                                EffectId::Distortion, EffectId::LoFiFx,
                                EffectId::Compressor};
    for (EffectId id : missing) {
        INFO(fx::effectName(id));
        CHECK_FALSE(rack.isAvailable(id));
    }
}

TEST_CASE("EffectRack routes audio through a selected MFX", "[fx][rack]") {
    // Selecting an effect that exists must change the audio; selecting one
    // that does not must leave it exactly alone. Both are what the panel
    // relies on when the user pages through the MFX list.
    constexpr double rate = 44100.0;

    fx::EffectRack rack;
    rack.prepare(rate, 4000);

    SECTION("Flanger is audible") {
        auto audio = sine(4000, 1000.0, rate);
        const auto dry = audio.left;
        rack.selectEffect(EffectId::Flanger);
        rack.reset();
        rack.setControls(0.5f, 1.0f, 0.5f);
        rack.process(audio.left.data(), audio.right.data(), audio.size());
        REQUIRE(allFinite(audio));
        CHECK(audio.left != dry);
    }

    SECTION("an unwritten MFX is a clean bypass") {
        auto audio = sine(4000, 1000.0, rate);
        const auto dry = audio.left;
        rack.selectEffect(EffectId::Slicer);
        rack.reset();
        rack.setControls(1.0f, 1.0f, 1.0f);
        rack.process(audio.left.data(), audio.right.data(), audio.size());
        CHECK(audio.left == dry);
    }
}

// ===========================================================================
// Reverb
// ===========================================================================

TEST_CASE("Reverb at LEVEL 0 passes the dry signal untouched", "[fx][reverb]") {
    constexpr double rate = 44100.0;
    auto audio = sine(2000, 440.0, rate);
    const auto dry = audio.left;

    fx::Reverb reverb;
    render(reverb, audio, rate, 1.0f, 0.5f, 0.0f);

    for (std::size_t i = 0; i < dry.size(); ++i)
        REQUIRE(audio.left[i] == dry[i]);
}

TEST_CASE("Reverb produces silence from silence", "[fx][reverb]") {
    constexpr double rate = 44100.0;
    Stereo audio(8000);

    fx::Reverb reverb;
    render(reverb, audio, rate, 1.0f, 0.0f, 1.0f);

    CHECK(peak(audio) == Approx(0.0f));
}

TEST_CASE("Reverb grows a tail after an impulse", "[fx][reverb]") {
    // The shortest comb is 1116 samples at 44.1 kHz, so nothing can come
    // back before then; after it, something must.
    constexpr double rate = 44100.0;
    auto audio = impulse(44100);

    fx::Reverb reverb;
    render(reverb, audio, rate, 0.5f, 0.5f, 1.0f);

    REQUIRE(allFinite(audio));
    CHECK(rms(audio.left, 1, 1100) == Approx(0.0).margin(1e-9));
    CHECK(rms(audio.left, 1200, 22050) > 1e-4);
}

TEST_CASE("Reverb TIME lengthens the tail", "[fx][reverb]") {
    // TIME is RT60, so the energy left one second after the impulse must
    // rise with the knob. That is the whole point of mapping decay time
    // rather than raw feedback.
    constexpr double rate = 44100.0;

    auto shortTail = impulse(88200);
    auto longTail  = impulse(88200);

    fx::Reverb a, b;
    render(a, shortTail, rate, 0.0f, 0.0f, 1.0f);
    render(b, longTail,  rate, 1.0f, 0.0f, 1.0f);

    REQUIRE(allFinite(shortTail));
    REQUIRE(allFinite(longTail));

    const double lateShort = rms(shortTail.left, 44100, 88200);
    const double lateLong  = rms(longTail.left,  44100, 88200);
    CHECK(lateLong > lateShort * 10.0);
}

TEST_CASE("Reverb TONE darkens and shortens the tail", "[fx][reverb]") {
    // Damping sits inside every comb loop, so a darker TONE also takes energy
    // out of each pass. Less late energy is the measurable consequence.
    constexpr double rate = 44100.0;

    auto open = impulse(44100);
    auto dark = impulse(44100);

    fx::Reverb a, b;
    render(a, open, rate, 0.7f, 0.0f, 1.0f);
    render(b, dark, rate, 0.7f, 1.0f, 1.0f);

    CHECK(rms(dark.left, 22050) < rms(open.left, 22050));
}

TEST_CASE("Reverb tail decays, and stays bounded with everything up",
          "[fx][reverb]") {
    constexpr double rate = 44100.0;

    SECTION("impulse response decays") {
        auto audio = impulse(4 * 44100);
        fx::Reverb reverb;
        render(reverb, audio, rate, 1.0f, 0.0f, 1.0f);
        REQUIRE(allFinite(audio));
        CHECK(rms(audio.left, 3 * 44100) < rms(audio.left, 2000, 44100));
    }

    SECTION("sustained tone at maximum TIME is finite and bounded") {
        // The class comment admits a tone on a comb resonance can exceed
        // unity. It must not exceed it by much, and never blow up.
        auto audio = sine(4 * 44100, 220.0, rate);
        fx::Reverb reverb;
        render(reverb, audio, rate, 1.0f, 0.0f, 1.0f);
        REQUIRE(allFinite(audio));
        CHECK(peak(audio) < 4.0f);
    }
}

// ===========================================================================
// Tape Echo
// ===========================================================================

TEST_CASE("Tape Echo at LEVEL 0 passes the dry signal untouched",
          "[fx][tapeecho]") {
    constexpr double rate = 1000.0;
    auto audio = sine(500, 50.0, rate);
    const auto dry = audio.left;

    fx::TapeEcho echo;
    render(echo, audio, rate, 0.5f, 1.0f, 0.0f);

    for (std::size_t i = 0; i < dry.size(); ++i)
        REQUIRE(audio.left[i] == dry[i]);
}

TEST_CASE("Tape Echo repeats an impulse one delay time later", "[fx][tapeecho]") {
    // RATE 0 is the minimum, 60 ms: 2646 samples at 44.1 kHz. The record head
    // runs through a 2x oversampler whose two 63-tap FIRs add 31 samples of
    // group delay INSIDE the loop, so every repeat lands 31 samples late. At
    // 44.1 kHz that is 0.7 ms and inaudible; at a 1 kHz test rate it would be
    // half the delay time, which is why this test does not use one. Wow and
    // flutter move the head by well under a sample, and the first repeat comes
    // off the tape before the head EQ touches it.
    constexpr double rate     = 44100.0;
    constexpr int    nominal  = 2646;
    constexpr int    firDelay = 31;
    auto audio = impulse(6000);

    fx::TapeEcho echo;
    render(echo, audio, rate, 0.0f, 0.0f, 1.0f);

    REQUIRE(allFinite(audio));
    const int where = loudestAfter(audio.left, 0);
    CHECK(where >= nominal + firDelay - 2);
    CHECK(where <= nominal + firDelay + 2);

    // The record head is a tanh at unity small-signal gain, so a full-scale
    // impulse comes back compressed, not at 1.0 and not at nothing.
    const float level = std::abs(audio.left[static_cast<std::size_t>(where)]);
    CHECK(level > 0.4f);
    CHECK(level < 1.0f);
}

TEST_CASE("Tape Echo with INTENSITY 0 repeats exactly once", "[fx][tapeecho]") {
    constexpr double rate = 1000.0;
    auto audio = impulse(400);

    fx::TapeEcho echo;
    render(echo, audio, rate, 0.0f, 0.0f, 1.0f);

    // The single repeat lands at 60 + 31 samples (delay plus oversampler
    // latency, see above). Well past that nothing may remain: no feedback, no
    // second echo.
    CHECK(rms(audio.left, 200) == Approx(0.0).margin(1e-6));
}

TEST_CASE("Tape Echo runaway is held by the tape, not by infinity",
          "[fx][tapeecho]") {
    // INTENSITY 1 puts the loop gain above unity on purpose. The waveshaper
    // ceiling must be what bounds it - forever, at every rate.
    constexpr double rate = 44100.0;

    for (float rateKnob : {0.0f, 0.5f, 1.0f}) {
        auto audio = sine(3 * 44100, 220.0, rate);

        fx::TapeEcho echo;
        render(echo, audio, rate, rateKnob, 1.0f, 1.0f);

        INFO("RATE " << rateKnob);
        REQUIRE(allFinite(audio));
        CHECK(peak(audio) < 3.0f);
    }
}

TEST_CASE("Tape Echo produces silence from silence", "[fx][tapeecho]") {
    constexpr double rate = 44100.0;
    Stereo audio(8000);

    fx::TapeEcho echo;
    render(echo, audio, rate, 0.5f, 1.0f, 1.0f);

    CHECK(peak(audio) == Approx(0.0f));
}

// ===========================================================================
// Chorus
// ===========================================================================

TEST_CASE("Chorus at LEVEL 0 passes the dry signal untouched", "[fx][chorus]") {
    constexpr double rate = 1000.0;
    auto audio = sine(500, 50.0, rate);
    const auto dry = audio.left;

    fx::Chorus chorus;
    render(chorus, audio, rate, 1.0f, 1.0f, 0.0f);

    for (std::size_t i = 0; i < dry.size(); ++i)
        REQUIRE(audio.left[i] == dry[i]);
}

TEST_CASE("Chorus at DEPTH 0 is a fixed 12 ms doubling", "[fx][chorus]") {
    // With the sweep parked, the wet path is a plain delay. DC in gives
    // exactly dry + wet = 2.0 once the line has filled; before the 12 ms
    // (12 samples at 1 kHz) it is dry alone.
    constexpr double rate = 1000.0;
    auto audio = constant(200, 1.0f);

    fx::Chorus chorus;
    render(chorus, audio, rate, 0.5f, 0.0f, 1.0f);

    CHECK(audio.left[5]   == Approx(1.0f));
    CHECK(audio.left[100] == Approx(2.0f));
    CHECK(audio.left[199] == Approx(2.0f));
}

TEST_CASE("Chorus detunes the two channels differently", "[fx][chorus]") {
    // The right channel reads the same LFO a quarter cycle on. With any depth
    // at all the channels must diverge; that divergence is the stereo width.
    constexpr double rate = 44100.0;
    auto audio = sine(8000, 440.0, rate);

    fx::Chorus chorus;
    render(chorus, audio, rate, 1.0f, 1.0f, 1.0f);

    REQUIRE(allFinite(audio));
    CHECK(channelsDiffer(audio));
}

TEST_CASE("Chorus is bounded and deterministic", "[fx][chorus]") {
    constexpr double rate = 44100.0;

    SECTION("dry plus wet cannot exceed twice the input") {
        auto audio = sine(8000, 440.0, rate);
        fx::Chorus chorus;
        render(chorus, audio, rate, 1.0f, 1.0f, 1.0f);
        REQUIRE(allFinite(audio));
        CHECK(peak(audio) <= 1.0f + 1e-4f);
    }

    SECTION("two instances render identically") {
        auto first  = sine(8000, 440.0, rate);
        auto second = sine(8000, 440.0, rate);
        fx::Chorus a, b;
        render(a, first,  rate, 0.7f, 0.8f, 1.0f);
        render(b, second, rate, 0.7f, 0.8f, 1.0f);
        CHECK(first.left == second.left);
        CHECK(first.right == second.right);
    }

    SECTION("silence in, silence out") {
        Stereo audio(4000);
        fx::Chorus chorus;
        render(chorus, audio, rate, 1.0f, 1.0f, 1.0f);
        CHECK(peak(audio) == Approx(0.0f));
    }
}

// ===========================================================================
// Flanger
// ===========================================================================

TEST_CASE("Flanger with the sweep parked is a fixed comb", "[fx][flanger]") {
    // DEPTH 0 holds the delay at the middle of its range, 3.25 ms. That comb
    // has notches at odd multiples of 1 / (2 * 3.25 ms) = 153.8 Hz and peaks
    // at even ones, and at DC it is unity: 0.5 * (x + x).
    constexpr double rate  = 44100.0;
    constexpr double delay = 0.5 * (0.5 + 6.0) * 0.001;  // seconds
    const double notchHz = 1.0 / (2.0 * delay);
    const double peakHz  = 1.0 / delay;

    SECTION("DC passes at unity") {
        auto audio = constant(2000, 1.0f);
        fx::Flanger flanger;
        render(flanger, audio, rate, 0.5f, 0.0f, 0.0f);
        CHECK(audio.left[1999] == Approx(1.0f).margin(1e-4));
    }

    SECTION("the first notch is deep") {
        auto audio = sine(16000, notchHz, rate);
        const auto dry = audio.left;
        fx::Flanger flanger;
        render(flanger, audio, rate, 0.5f, 0.0f, 0.0f);
        CHECK(rms(audio.left, 4000) < 0.1 * rms(dry, 4000));
    }

    SECTION("the first peak passes at unity") {
        auto audio = sine(16000, peakHz, rate);
        const auto dry = audio.left;
        fx::Flanger flanger;
        render(flanger, audio, rate, 0.5f, 0.0f, 0.0f);
        CHECK(rms(audio.left, 4000) == Approx(rms(dry, 4000)).epsilon(0.05));
    }
}

TEST_CASE("Flanger RES sharpens the peaks", "[fx][flanger]") {
    // Feedback raises the comb's peaks above unity - that is what makes the
    // sweep whistle. More RES must mean more level at the peak frequency.
    constexpr double rate   = 44100.0;
    const double     peakHz = 1.0 / (3.25 * 0.001);

    auto flat = sine(16000, peakHz, rate);
    auto res  = sine(16000, peakHz, rate);

    fx::Flanger a, b;
    render(a, flat, rate, 0.5f, 0.0f, 0.0f);
    render(b, res,  rate, 0.5f, 0.0f, 1.0f);

    REQUIRE(allFinite(res));
    CHECK(rms(res.left, 8000) > rms(flat.left, 8000) * 1.5);
}

TEST_CASE("Flanger sweeps the two channels in opposite directions",
          "[fx][flanger]") {
    constexpr double rate = 44100.0;
    auto audio = sine(8000, 1000.0, rate);

    fx::Flanger flanger;
    render(flanger, audio, rate, 1.0f, 1.0f, 0.0f);

    REQUIRE(allFinite(audio));
    CHECK(channelsDiffer(audio));
}

TEST_CASE("Flanger stays bounded with RES and DEPTH at maximum",
          "[fx][flanger]") {
    constexpr double rate = 44100.0;

    for (float rateKnob : {0.0f, 0.5f, 1.0f}) {
        auto audio = sine(3 * 44100, 220.0, rate);

        fx::Flanger flanger;
        render(flanger, audio, rate, rateKnob, 1.0f, 1.0f);

        INFO("RATE " << rateKnob);
        REQUIRE(allFinite(audio));
        CHECK(peak(audio) < 3.0f);
    }
}

TEST_CASE("Flanger produces silence from silence", "[fx][flanger]") {
    constexpr double rate = 44100.0;
    Stereo audio(8000);

    fx::Flanger flanger;
    render(flanger, audio, rate, 1.0f, 1.0f, 1.0f);

    CHECK(peak(audio) == Approx(0.0f));
}

// ===========================================================================
// Phaser
// ===========================================================================

TEST_CASE("Phaser with the sweep parked has its notches where the maths says",
          "[fx][phaser]") {
    // Four first-order allpasses cornered at 800 Hz. Each contributes
    // -2 atan(tan(pi f / fs) / tan(pi fc / fs)) of phase, so the cascade is
    // at -180 degrees (a notch against the dry) where each stage is at -45,
    // i.e. tan(pi f / fs) = tan(22.5 deg) * tan(pi fc / fs), and at -360 (a
    // peak, unity) at fc itself. At DC an allpass is +1, so DC passes at unity.
    constexpr double rate = 44100.0;
    constexpr double fc   = 800.0;

    const double tc      = std::tan(kPi * fc / rate);
    const double notchHz = std::atan(std::tan(kPi / 8.0) * tc) * rate / kPi;

    SECTION("DC passes at unity") {
        auto audio = constant(4000, 1.0f);
        fx::Phaser phaser;
        render(phaser, audio, rate, 0.5f, 0.0f, 0.0f);
        CHECK(audio.left[3999] == Approx(1.0f).margin(1e-3));
    }

    SECTION("the lower notch is deep") {
        auto audio = sine(16000, notchHz, rate);
        const auto dry = audio.left;
        fx::Phaser phaser;
        render(phaser, audio, rate, 0.5f, 0.0f, 0.0f);
        REQUIRE(allFinite(audio));
        CHECK(rms(audio.left, 4000) < 0.1 * rms(dry, 4000));
    }

    SECTION("the centre frequency passes at unity") {
        auto audio = sine(16000, fc, rate);
        const auto dry = audio.left;
        fx::Phaser phaser;
        render(phaser, audio, rate, 0.5f, 0.0f, 0.0f);
        CHECK(rms(audio.left, 4000) == Approx(rms(dry, 4000)).epsilon(0.05));
    }
}

TEST_CASE("Phaser RES raises the peaks", "[fx][phaser]") {
    constexpr double rate = 44100.0;

    auto flat = sine(16000, 800.0, rate);
    auto res  = sine(16000, 800.0, rate);

    fx::Phaser a, b;
    render(a, flat, rate, 0.5f, 0.0f, 0.0f);
    render(b, res,  rate, 0.5f, 0.0f, 1.0f);

    REQUIRE(allFinite(res));
    CHECK(rms(res.left, 8000) > rms(flat.left, 8000) * 1.2);
}

TEST_CASE("Phaser RES does not turn into a bass boost", "[fx][phaser]") {
    // The feedback path carries a DC blocker precisely because an allpass
    // passes DC at +1: without it, RES at maximum would lift DC by
    // 1 / (1 - 0.6) = 2.5x. With it, DC must still come out near unity.
    constexpr double rate = 44100.0;
    auto audio = constant(2 * 44100, 1.0f);

    fx::Phaser phaser;
    render(phaser, audio, rate, 0.5f, 0.0f, 1.0f);

    REQUIRE(allFinite(audio));
    CHECK(audio.left[2 * 44100 - 1] == Approx(1.0f).margin(0.05));
}

TEST_CASE("Phaser sweeps the two channels apart", "[fx][phaser]") {
    constexpr double rate = 44100.0;
    auto audio = sine(8000, 1000.0, rate);

    fx::Phaser phaser;
    render(phaser, audio, rate, 1.0f, 1.0f, 0.0f);

    REQUIRE(allFinite(audio));
    CHECK(channelsDiffer(audio));
}

TEST_CASE("Phaser stays bounded with RES and DEPTH at maximum", "[fx][phaser]") {
    constexpr double rate = 44100.0;

    for (float rateKnob : {0.0f, 0.5f, 1.0f}) {
        auto audio = sine(3 * 44100, 220.0, rate);

        fx::Phaser phaser;
        render(phaser, audio, rate, rateKnob, 1.0f, 1.0f);

        INFO("RATE " << rateKnob);
        REQUIRE(allFinite(audio));
        CHECK(peak(audio) < 3.0f);
    }
}

TEST_CASE("Phaser produces silence from silence", "[fx][phaser]") {
    constexpr double rate = 44100.0;
    Stereo audio(8000);

    fx::Phaser phaser;
    render(phaser, audio, rate, 1.0f, 1.0f, 1.0f);

    CHECK(peak(audio) == Approx(0.0f));
}
