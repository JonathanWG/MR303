#include "sp303/fx/Reverb.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void Reverb::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Tunings are specified at 44.1 kHz; scaling them keeps the room the same
    // size at every host rate instead of shrinking it at 96 kHz.
    const double scale = sampleRate_ / kTuningRate;

    double totalCombDelay = 0.0;

    for (std::size_t ch = 0; ch < channels_.size(); ++ch) {
        auto& channel = channels_[ch];
        const int spread = (ch == 0) ? 0 : kStereoSpread;

        for (std::size_t i = 0; i < kCombs; ++i) {
            const double delay = std::max(
                1.0, std::round((kCombTuning[i] + spread) * scale));
            channel.combDelay[i] = delay;
            channel.combs[i].prepare(static_cast<int>(delay));
            totalCombDelay += delay;
        }

        for (std::size_t i = 0; i < kAllpasses; ++i) {
            const double delay = std::max(
                1.0, std::round((kAllpassTuning[i] + spread) * scale));
            channel.allpassDelay[i] = delay;
            channel.allpasses[i].prepare(static_cast<int>(delay));
        }
    }

    meanCombSeconds_ = totalCombDelay / (2.0 * kCombs) / sampleRate_;

    dirty_ = true;
    updateCoefficients();
    reset();
}

void Reverb::reset() noexcept {
    for (auto& channel : channels_) {
        for (auto& comb : channel.combs)        comb.reset();
        for (auto& allpass : channel.allpasses) allpass.reset();
        channel.combState.fill(0.0f);
    }
}

void Reverb::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    if (c1 != time_ || c2 != tone_) dirty_ = true;

    time_  = c1;
    tone_  = c2;
    level_ = c3;
}

void Reverb::updateCoefficients() noexcept {
    // Decay first, feedback second. RT60 is what the ear hears, so the knob
    // is exponential in RT60, and the feedback a comb of length T needs to
    // decay 60 dB in that time falls out as g = 10^(-3 T / RT60).
    const double rt60 = kMinDecaySeconds
                      * std::pow(kMaxDecaySeconds / kMinDecaySeconds,
                                 static_cast<double>(time_));
    const double g = std::pow(10.0, -3.0 * meanCombSeconds_ / rt60);
    feedback_ = static_cast<float>(std::min(g, kMaxFeedback));

    // One-pole lowpass in every comb loop. Clamped under Nyquist so a low
    // test rate does not turn the damper into a gain.
    const double cutoff = std::min(
        kMaxDampHz * std::pow(kMinDampHz / kMaxDampHz, static_cast<double>(tone_)),
        0.45 * sampleRate_);
    damp_ = static_cast<float>(std::exp(-2.0 * kPi * cutoff / sampleRate_));

    dirty_ = false;
}

void Reverb::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;
    if (dirty_) updateCoefficients();

    const float feedback = feedback_;
    const float damp     = damp_;
    const float wetGain  = level_ * kWetGain;

    for (int n = 0; n < numFrames; ++n) {
        // Mono into both tanks. The spread between the channels' tunings, not
        // two input paths, is what makes the tail stereo.
        const float in = (left[n] + right[n]) * 0.5f * kInputGain;

        float wet[2] {0.0f, 0.0f};

        for (std::size_t ch = 0; ch < 2; ++ch) {
            auto& channel = channels_[ch];
            float acc = 0.0f;

            for (std::size_t i = 0; i < kCombs; ++i) {
                const float y = channel.combs[i].read(channel.combDelay[i] - 1.0);

                // Damping sits inside the loop so each pass is darker than
                // the last, which is what makes a tail recede instead of
                // buzzing at a fixed colour until it stops.
                float& state = channel.combState[i];
                state = y + (state - y) * damp;

                // A tail that has decayed below hearing must not slow the
                // audio thread down with denormals.
                if (std::abs(state) < 1.0e-18f) state = 0.0f;

                channel.combs[i].write(in + state * feedback);
                acc += y;
            }

            for (std::size_t i = 0; i < kAllpasses; ++i) {
                const float delayed =
                    channel.allpasses[i].read(channel.allpassDelay[i] - 1.0);
                channel.allpasses[i].write(acc + delayed * kAllpassGain);
                acc = delayed - acc;
            }

            wet[ch] = acc;
        }

        left[n]  += wet[0] * wetGain;
        right[n] += wet[1] * wetGain;
    }
}

}  // namespace sp303::fx
