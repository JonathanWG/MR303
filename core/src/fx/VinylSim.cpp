#include "sp303/fx/VinylSim.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Expected clicks per second at NOISE = 1. Sparse on purpose: dense crackle
// stops reading as a surface defect and starts reading as distortion.
constexpr double kMaxCracklePerSecond = 90.0;

// How fast a click decays, as a per-sample multiplier at 44.1 kHz. Scaled to
// the running rate in prepare() would be more correct; the audible difference
// across supported rates is small enough that a fixed time constant is used.
constexpr double kCrackleDecaySeconds = 0.004;

}  // namespace

void VinylSim::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    centreFrames_ = kDelayCentreMs * 0.001 * sampleRate_;

    const auto maxSamples = static_cast<int>(
        std::ceil((kDelayCentreMs + kMaxWowMs + 1.0) * 0.001 * sampleRate_));

    // Distinct seeds: identical noise in both channels collapses to a mono
    // hiss pinned dead centre, which sounds synthetic next to the audio.
    std::uint32_t seed = 0x9E3779B9u;
    for (auto& channel : channels_) {
        channel.line.prepare(maxSamples);
        channel.noiseShaper.setBandpass(2500.0, 0.6, sampleRate_);
        channel.rng = seed;
        seed = seed * 1664525u + 1013904223u;
    }

    wowStep_     = kWowHz     / sampleRate_;
    flutterStep_ = kFlutterHz / sampleRate_;

    dirty_ = true;
    updateCoefficients();
    reset();
}

void VinylSim::reset() noexcept {
    for (auto& channel : channels_) {
        channel.line.reset();
        channel.highpass.reset();
        channel.lowpass.reset();
        channel.noiseShaper.reset();
        channel.crackle = 0.0f;
    }
    wowPhase_     = 0.0;
    flutterPhase_ = 0.0;
}

void VinylSim::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    if (c3 != tone_) dirty_ = true;

    noise_ = c1;
    wow_   = c2;
    tone_  = c3;
}

void VinylSim::updateCoefficients() noexcept {
    // Both ends close in together, exponentially: the band narrows by a
    // constant ratio per unit of knob travel, which is what "getting duller"
    // sounds like.
    const double t  = static_cast<double>(tone_);
    const double hp = kHighpassMinHz
                    * std::pow(kHighpassMaxHz / kHighpassMinHz, t);
    const double lp = kLowpassMaxHz
                    * std::pow(kLowpassMinHz / kLowpassMaxHz, t);

    for (auto& channel : channels_) {
        channel.highpass.setHighpass(hp, 0.707, sampleRate_);
        channel.lowpass.setLowpass(lp, 0.707, sampleRate_);
    }

    dirty_ = false;
}

float VinylSim::nextUniform(Channel& channel) noexcept {
    channel.rng ^= channel.rng << 13;
    channel.rng ^= channel.rng >> 17;
    channel.rng ^= channel.rng << 5;
    return static_cast<float>(channel.rng)
         / static_cast<float>(0xFFFFFFFFu);  // [0, 1]
}

void VinylSim::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;
    if (dirty_) updateCoefficients();

    const double wowDepth = static_cast<double>(wow_) * kMaxWowMs * 0.001 * sampleRate_;

    const float hissGain     = noise_ * 0.03f;
    const float crackleGain  = noise_ * 0.5f;
    const float crackleOdds  = static_cast<float>(
        noise_ * kMaxCracklePerSecond / sampleRate_);
    const float crackleDecay = static_cast<float>(
        std::exp(-1.0 / (kCrackleDecaySeconds * sampleRate_)));

    for (int n = 0; n < numFrames; ++n) {
        wowPhase_     += wowStep_;
        flutterPhase_ += flutterStep_;
        if (wowPhase_     >= 1.0) wowPhase_     -= 1.0;
        if (flutterPhase_ >= 1.0) flutterPhase_ -= 1.0;

        // Wow carries most of the depth, flutter rides on top of it. Summing
        // two incommensurate rates is what keeps the wobble from sounding
        // like a plain LFO.
        const double modulation =
            std::sin(2.0 * kPi * wowPhase_) * 0.8
          + std::sin(2.0 * kPi * flutterPhase_) * 0.2;

        const double delayFrames = centreFrames_ + modulation * wowDepth;

        for (int index = 0; index < 2; ++index) {
            float* data    = (index == 0) ? left : right;
            auto&  channel = channels_[static_cast<std::size_t>(index)];

            channel.line.write(data[n]);
            float v = channel.line.read(delayFrames);

            // Hiss is band-shaped rather than white: unfiltered white noise
            // sits far brighter than anything a record produces.
            const float white = nextUniform(channel) * 2.0f - 1.0f;
            v += channel.noiseShaper.process(white) * hissGain;

            // Sparse impulses with an exponential tail. Retriggering keeps the
            // louder of the two envelopes so a dense passage does not sound
            // quieter than a sparse one.
            channel.crackle *= crackleDecay;
            if (nextUniform(channel) < crackleOdds) {
                const float amplitude = (nextUniform(channel) * 2.0f - 1.0f) * crackleGain;
                if (std::abs(amplitude) > std::abs(channel.crackle))
                    channel.crackle = amplitude;
            }
            v += channel.crackle;

            v = channel.highpass.process(v);
            v = channel.lowpass.process(v);

            data[n] = v;
        }
    }
}

}  // namespace sp303::fx
