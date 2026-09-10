#include "sp303/voice/VoiceManager.h"

#include <algorithm>
#include <limits>

namespace sp303 {

void VoiceManager::prepare(double hostSampleRate) noexcept {
    for (auto& v : voices_) {
        v.prepare(hostSampleRate);
        v.setInterpolationMode(interp_);
    }
    Voice::resetGlobalOrder();
}

void VoiceManager::setPolyphony(int voices) noexcept {
    polyphony_ = std::clamp(voices, 1, kMaxVoices);
}

void VoiceManager::setInterpolationMode(dsp::InterpolationMode mode) noexcept {
    interp_ = mode;
    for (auto& v : voices_) v.setInterpolationMode(mode);
}

Voice* VoiceManager::findFreeVoice() noexcept {
    for (int i = 0; i < polyphony_; ++i)
        if (!voices_[static_cast<std::size_t>(i)].isActive())
            return &voices_[static_cast<std::size_t>(i)];
    return nullptr;
}

Voice* VoiceManager::stealOldestVoice() noexcept {
    Voice*        oldest = nullptr;
    std::uint64_t best   = std::numeric_limits<std::uint64_t>::max();

    for (int i = 0; i < polyphony_; ++i) {
        auto& v = voices_[static_cast<std::size_t>(i)];
        if (v.isActive() && v.startOrder() < best) {
            best   = v.startOrder();
            oldest = &v;
        }
    }
    return oldest;
}

void VoiceManager::noteOn(int padIndex, const Pad& pad,
                          const SampleBuffer* buffer) noexcept {
    if (pad.isEmpty() || buffer == nullptr) return;

    // Retrigger semantics: hitting a pad that is already sounding restarts it
    // rather than layering. This is what makes fast stutter/roll playing work
    // and matches the hardware.
    for (int i = 0; i < polyphony_; ++i) {
        auto& v = voices_[static_cast<std::size_t>(i)];
        if (v.isActive() && v.padIndex() == padIndex) {
            v.start(pad, buffer, padIndex);
            return;
        }
    }

    Voice* voice = findFreeVoice();
    if (voice == nullptr) voice = stealOldestVoice();
    if (voice == nullptr) return;

    voice->start(pad, buffer, padIndex);
}

void VoiceManager::noteOff(int padIndex) noexcept {
    for (auto& v : voices_)
        if (v.isActive() && v.padIndex() == padIndex)
            v.release();
}

void VoiceManager::allNotesOff() noexcept {
    for (auto& v : voices_) v.stop();

    // Drop the hazards immediately rather than waiting for the next block. A
    // host that stops the transport and never calls processBlock again would
    // otherwise pin every buffer forever.
    hazards_.clearAll();
}

void VoiceManager::publishHazards() noexcept {
    // Published AFTER rendering, so a pointer a voice picked up during this
    // block is visible to the collector before the next block starts. The
    // retirement rule in Device waits for exactly that boundary.
    for (std::size_t i = 0; i < voices_.size(); ++i)
        hazards_.protect(i, voices_[i].bufferPointer());
}

int VoiceManager::activeVoiceCount() const noexcept {
    int count = 0;
    for (const auto& v : voices_)
        if (v.isActive()) ++count;
    return count;
}

void VoiceManager::render(float* left, float* right, int numFrames) noexcept {
    if (left == nullptr || right == nullptr) return;

    for (int i = 0; i < polyphony_; ++i) {
        auto& v = voices_[static_cast<std::size_t>(i)];
        if (!v.isActive()) continue;

        for (int n = 0; n < numFrames; ++n) {
            if (!v.isActive()) break;
            v.renderNextFrame(left[n], right[n]);
        }
    }

    publishHazards();
}

}  // namespace sp303
