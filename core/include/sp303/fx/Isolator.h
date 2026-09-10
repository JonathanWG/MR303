#pragma once

#include <array>

#include "sp303/dsp/Biquad.h"
#include "sp303/fx/IEffect.h"

namespace sp303::fx {

// ---------------------------------------------------------------------------
// Isolator - direct-access button 5.
//
//   CTRL 1 = LOW     CTRL 2 = MID     CTRL 3 = HIGH
//
// A three-band kill EQ, not a tone control. The point of an isolator is that a
// band can be taken to actual silence and brought back, which is why the taper
// runs to zero at the bottom of the knob rather than to some polite -12 dB.
// Centre is unity, top is a modest lift.
//
// Crossovers are Linkwitz-Riley 4th order (two cascaded Butterworth biquads
// per slope). LR4 is used because its bands sum flat in MAGNITUDE when all
// three knobs are centred - with a naive filter split, "everything at noon"
// would already colour the signal, and every performance move would start
// from a lie. Measured at 44.1 kHz: within +/-0.02 dB from 50 Hz to 16 kHz,
// both crossovers included.
//
// Flat in magnitude, NOT in phase: an LR4 band split sums to an allpass, not
// to the identity, so the output is not sample-identical to the input even at
// noon. That is inherent to the topology and inaudible on its own; it only
// matters if this is ever parallelled against a dry path.
//
// STATUS: NOT matched to hardware. Crossover frequencies, slope order and the
// gain taper are placeholders. UNVERIFIED - see docs/HARDWARE_FACTS.md.
// ---------------------------------------------------------------------------
class Isolator final : public IEffect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    SP303_RT void reset() noexcept override;
    SP303_RT void setControls(float ctrl1, float ctrl2, float ctrl3) noexcept override;
    SP303_RT void process(float* left, float* right, int numFrames) noexcept override;

    EffectId id() const noexcept override { return EffectId::Isolator; }
    const char* ctrl1Name() const noexcept override { return "LOW"; }
    const char* ctrl2Name() const noexcept override { return "MID"; }
    const char* ctrl3Name() const noexcept override { return "HIGH"; }

private:
    static constexpr double kLowCrossoverHz  = 200.0;
    static constexpr double kHighCrossoverHz = 4000.0;

    // Butterworth Q. Two of these in series is a Linkwitz-Riley 4th order.
    static constexpr double kButterworthQ = 0.70710678118654752;

    // Below this the band is muted outright, so the knob has a real zero.
    static constexpr float kSilenceThreshold = 0.001f;
    static constexpr double kMinBandDb =-60.0;
    static constexpr double kMaxBandDb =  6.0;

    struct Bands {
        std::array<dsp::Biquad, 2> lowpassLow;    // LR4 low band
        std::array<dsp::Biquad, 2> highpassLow;   // LR4 split at kLowCrossoverHz
        std::array<dsp::Biquad, 2> lowpassHigh;   // LR4 split at kHighCrossoverHz
        std::array<dsp::Biquad, 2> highpassHigh;  // LR4 high band
    };

    void updateCoefficients() noexcept;

    std::array<Bands, 2> channels_ {};  // L, R

    double sampleRate_ {44100.0};

    float lowGain_  {1.0f};
    float midGain_  {1.0f};
    float highGain_ {1.0f};
};

}  // namespace sp303::fx
