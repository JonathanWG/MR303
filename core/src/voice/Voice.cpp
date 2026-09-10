#include "sp303/voice/Voice.h"

#include <algorithm>
#include <cmath>

namespace sp303 {

void Voice::prepare(double hostSampleRate) noexcept {
    hostRate_ = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    stop();
}

void Voice::start(const Pad& pad, const SampleBuffer* buffer, int padIndex) noexcept {
    if (buffer == nullptr || buffer->isEmpty()) {
        stop();
        return;
    }

    buffer_   = buffer;
    pad_      = pad;
    padIndex_ = padIndex;

    const auto frames = static_cast<std::uint32_t>(buffer_->numFrames());

    regionStart_ = std::min(pad.startFrame, frames);
    regionEnd_   = (pad.endFrame == 0 || pad.endFrame > frames) ? frames : pad.endFrame;
    if (regionEnd_ <= regionStart_) regionEnd_ = frames;

    // Varispeed: playback rate is the pad's ratio corrected for the difference
    // between the sample's native rate and the host rate.
    const double rateRatio = buffer_->sourceRate() / hostRate_;
    increment_ = rateRatio * static_cast<double>(std::max(pad.speedRatio, 0.01f));

    position_   = pad.reverse ? static_cast<double>(regionEnd_) - 1.0
                              : static_cast<double>(regionStart_);
    active_     = true;
    released_   = false;
    startOrder_ = nextOrder_++;
}

void Voice::release() noexcept {
    released_ = true;

    // HOLD latches playback past the release; Trigger plays to END regardless.
    // Only Gate without HOLD actually stops here.
    if (pad_.trigger == TriggerMode::Gate && !pad_.hold)
        stop();
}

void Voice::stop() noexcept {
    active_   = false;
    released_ = false;
    padIndex_ = -1;
    position_ = 0.0;

    // Clearing a raw pointer, not dropping a reference. This used to be
    // `buffer_.reset()`, which could run ~SampleBuffer - and therefore free
    // memory - on the real-time thread whenever the last voice on a replaced
    // sample finished.
    buffer_ = nullptr;
}

void Voice::advance() noexcept {
    position_ += pad_.reverse ? -increment_ : increment_;

    const auto start = static_cast<double>(regionStart_);
    const auto end   = static_cast<double>(regionEnd_);

    const bool pastEnd = pad_.reverse ? (position_ < start) : (position_ >= end);
    if (!pastEnd) return;

    if (pad_.loop == LoopMode::Loop) {
        const double span = end - start;
        if (span <= 0.0) { stop(); return; }

        // Wrap by the overshoot rather than snapping to the boundary: at high
        // speed ratios a snap loses fractional position and makes loops drift
        // audibly out of time.
        if (pad_.reverse) position_ += span;
        else              position_ -= span;
    } else {
        stop();
    }
}

void Voice::renderNextFrame(float& outL, float& outR) noexcept {
    if (!active_ || buffer_ == nullptr) return;

    const int   frames = buffer_->numFrames();
    const float gain   = pad_.level;

    const float* left  = buffer_->channel(0);
    if (left == nullptr) { stop(); return; }

    const float* right = buffer_->numChannels() > 1 ? buffer_->channel(1) : left;

    outL += dsp::Interpolator::read(interp_, left,  frames, position_) * gain;
    outR += dsp::Interpolator::read(interp_, right, frames, position_) * gain;

    advance();
}

}  // namespace sp303
