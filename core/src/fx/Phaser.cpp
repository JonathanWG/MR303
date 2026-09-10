#include "sp303/fx/Phaser.h"

#include <algorithm>
#include <cmath>

namespace sp303::fx {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void Phaser::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const double highpassHz = std::min(kFeedbackHighpassHz, 0.05 * sampleRate_);
    for (auto& channel : channels_)
        channel.dcBlock.setHighpass(highpassHz, 0.707, sampleRate_);

    lfo_.prepare(sampleRate_);
    lfo_.setRate(kMinRateHz);

    reset();
}

void Phaser::reset() noexcept {
    for (auto& channel : channels_) {
        for (auto& stage : channel.stages) stage = Stage {};
        channel.dcBlock.reset();
        channel.last = 0.0f;
    }
    lfo_.reset();
}

void Phaser::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    const float c1 = std::clamp(ctrl1, 0.0f, 1.0f);
    const float c2 = std::clamp(ctrl2, 0.0f, 1.0f);
    const float c3 = std::clamp(ctrl3, 0.0f, 1.0f);

    lfo_.setRate(kMinRateHz * std::pow(kMaxRateHz / kMinRateHz,
                                        static_cast<double>(c1)));
    depth_    = c2;
    feedback_ = c3 * kMaxFeedback;
}

void Phaser::process(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    const double maxCornerHz = 0.45 * sampleRate_;
    const double sweep       = static_cast<double>(depth_) * kMaxSweepOctaves;

    for (int n = 0; n < numFrames; ++n) {
        lfo_.advance();

        for (std::size_t ch = 0; ch < 2; ++ch) {
            float* data    = (ch == 0) ? left : right;
            auto&  channel = channels_[ch];

            // Corner frequency, exponential in the LFO so the sweep covers the
            // same interval above and below the centre.
            const double octaves = lfo_.triangle(ch == 0 ? 0.0 : kRightPhaseOffset) * sweep;
            const double corner  = std::clamp(kCentreHz * std::exp2(octaves),
                                              kMinCornerHz, maxCornerHz);

            // First-order allpass, -90 degrees at the corner.
            const double t = std::tan(kPi * corner / sampleRate_);
            const float  a = static_cast<float>((t - 1.0) / (t + 1.0));

            const float dry = data[n];

            // Feedback from the previous frame's cascade output, DC-blocked.
            float v = dry + channel.dcBlock.process(channel.last) * feedback_;

            for (auto& stage : channel.stages) {
                const float y = a * v + stage.x1 - a * stage.y1;
                stage.x1 = v;
                stage.y1 = y;
                v = y;
            }

            channel.last = v;
            data[n] = 0.5f * (dry + v);
        }
    }
}

}  // namespace sp303::fx
