#pragma once

#include <array>
#include <cstdint>

#include "sp303/dsp/Biquad.h"
#include "sp303/dsp/DelayLine.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Vinyl Sim - direct-access button 4.
//
//   CTRL 1 = NOISE     CTRL 2 = WOW     CTRL 3 = TONE
//
// Three independent things people mean by "sounds like a record", each on its
// own knob rather than bundled into one "amount":
//
//   NOISE  surface noise - a filtered hiss floor plus sparse crackle impulses
//   WOW    pitch instability - a slow wow and a faster flutter, summed
//   TONE   bandwidth - closes in from both ends toward a midrange-only signal
//
// The RNG is a deterministic xorshift seeded per channel, NOT std::random.
// Deterministic matters twice: golden-file regression tests must be stable,
// and rendering the same project twice must produce the same audio.
//
// STATUS: NOT matched to hardware. Noise spectrum, crackle density, wow rate
// and depth, and the tone curve are all invented placeholders. UNVERIFIED -
// see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class VinylSim final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::VinylSim; }
    const char* ctrl1Name() const noexcept override { return "NOISE"; }
    const char* ctrl2Name() const noexcept override { return "WOW"; }
    const char* ctrl3Name() const noexcept override { return "TONE"; }

private:
    // 33 1/3 rpm is 0.556 Hz - a wow that tracks one revolution per turn.
    static constexpr double kWowHz     = 0.5556;
    static constexpr double kFlutterHz = 6.3;

    // Peak pitch modulation, expressed as delay depth.
    static constexpr double kMaxWowMs = 3.0;

    // Headroom for the modulated read head, plus its centre offset.
    static constexpr double kDelayCentreMs = 4.0;

    // Tone endpoints. At TONE = 0 the band is wide open; at 1 it is a
    // midrange window that reads as a worn record through a small speaker.
    static constexpr double kHighpassMinHz = 20.0;
    static constexpr double kHighpassMaxHz = 300.0;
    static constexpr double kLowpassMaxHz  = 18000.0;
    static constexpr double kLowpassMinHz  = 3000.0;

    struct Channel {
        dsp::DelayLine line;
        dsp::Biquad    highpass;
        dsp::Biquad    lowpass;
        dsp::Biquad    noiseShaper;  // tilts white noise toward surface hiss
        std::uint32_t  rng {0x9E3779B9u};
        float          crackle {0.0f};  // decaying envelope of the last click
    };

    SP303_RT float nextUniform(Channel& channel) noexcept;
    void updateCoefficients() noexcept;

    std::array<Channel, 2> channels_ {};

    double sampleRate_  {44100.0};
    double wowPhase_    {0.0};
    double flutterPhase_{0.0};
    double wowStep_     {0.0};
    double flutterStep_ {0.0};
    double centreFrames_{0.0};

    float noise_ {0.0f};
    float wow_   {0.0f};
    float tone_  {0.0f};
    bool  dirty_ {true};
};

}  // namespace sp303::fx
