#include "sp303/SampleBuffer.h"

#include <utility>

namespace sp303 {

SampleBuffer::SampleBuffer(std::vector<std::vector<float>> channels,
                           double sourceSampleRate,
                           std::string name)
    : channels_(std::move(channels)),
      sourceRate_(sourceSampleRate),
      name_(std::move(name)) {
    numChannels_ = static_cast<int>(channels_.size());
    numFrames_   = channels_.empty() ? 0 : static_cast<int>(channels_[0].size());

    // Ragged channels would let the audio thread read past the end of a short
    // one. Pad up front so the invariant "every channel has numFrames_ samples"
    // holds for the lifetime of the object.
    for (auto& ch : channels_)
        ch.resize(static_cast<std::size_t>(numFrames_), 0.0f);
}

double SampleBuffer::estimateBpm(int bars, int beatsPerBar) const noexcept {
    if (numFrames_ <= 0 || sourceRate_ <= 0.0 || bars <= 0 || beatsPerBar <= 0)
        return 0.0;

    const double seconds = static_cast<double>(numFrames_) / sourceRate_;
    if (seconds <= 0.0) return 0.0;

    const double beats = static_cast<double>(bars) * static_cast<double>(beatsPerBar);
    return (beats / seconds) * 60.0;
}

}  // namespace sp303
