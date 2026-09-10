#include "sp303/Device.h"

#include <algorithm>
#include <cmath>

namespace sp303 {

Device::Device() = default;

void Device::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    voices_.prepare(sampleRate_);
    effects_.prepare(sampleRate_, maxBlockSize);
    sequencer_.prepare(sampleRate_);

    // Capture budget follows the emulated memory limit of the current quality
    // mode, so running out mid-take is the hardware's constraint rather than
    // an arbitrary one. Lo-Fi is the long one at 3'10".
    resampler_.prepare(sampleRate_, internalSecondsFor(quality_));

    setQualityMode(quality_);

    // 16-bit placeholder. UNVERIFIED - see dsp/Quantizer.h. Phase 0 replaces
    // this with the measured value; it is a one-line change by design.
    for (auto* q : {&quantizerL_, &quantizerR_}) {
        q->setBitDepth(16);
        q->setDitherMode(dsp::DitherMode::Round);
    }

    reset();
}

void Device::reset() noexcept {
    voices_.allNotesOff();
    effects_.reset();
    sequencer_.reset();
    decimatorL_.reset();
    decimatorR_.reset();
}

void Device::setQualityMode(QualityMode mode) {
    quality_ = mode;
    decimatorL_.prepare(sampleRate_, mode);
    decimatorR_.prepare(sampleRate_, mode);

    // The capture budget moves with the mode. Allocates - callers must have
    // suspended processing first.
    resampler_.prepare(sampleRate_, internalSecondsFor(mode));
}

void Device::setFidelityMode(FidelityMode mode) noexcept {
    fidelity_ = mode;
    // Authentic keeps the hardware's 8-voice ceiling. Modern currently matches
    // it; raising it is a deliberate future decision, not a default.
    voices_.setPolyphony(kMaxVoices);
}

void Device::armResample(int destinationSlot) noexcept {
    resampler_.arm(destinationSlot, ResampleRecorder::Source::Output);
}

void Device::armInputSampling(int destinationSlot) noexcept {
    resampler_.arm(destinationSlot, ResampleRecorder::Source::Input);
}

void Device::setSlotSample(int slotIndex, SampleBufferPtr buffer) {
    if (slotIndex < 0 || slotIndex >= kNumSlots) return;

    auto displaced =
        slots_[static_cast<std::size_t>(slotIndex)].publish(std::move(buffer));

    // Not dropped here: a block already in flight may have loaded its pointer
    // a moment ago and be about to hand it to a voice.
    if (displaced)
        retired_.push_back({std::move(displaced), blockCount()});

    // Opportunistic: clears out anything retired by an earlier load.
    (void)collectRetiredSamples();
}

SampleBufferPtr Device::slotSample(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kNumSlots) return {};
    return slots_[static_cast<std::size_t>(slotIndex)].owned();
}

int Device::collectRetiredSamples() {
    if (retired_.empty()) return 0;

    const auto now      = blockCount();
    const auto& hazards = voices_.hazards();

    const auto before = retired_.size();

    // Two conditions, both necessary:
    //
    //   retiredAtBlock != now   the block that was in flight when this buffer
    //                           was displaced has finished, so no voice can
    //                           still be picking the pointer up unprotected
    //   !isProtected            and no voice that already picked it up is
    //                           still playing it
    //
    // If the audio thread never runs again the counter never moves and nothing
    // is freed. That is a bounded leak while stopped, and it is the safe side
    // to err on: allNotesOff() clears the hazards, and the next block releases
    // everything.
    retired_.erase(
        std::remove_if(retired_.begin(), retired_.end(),
                       [&](const RetiredSample& entry) {
                           return entry.retiredAtBlock != now
                               && !hazards.isProtected(entry.buffer.get());
                       }),
        retired_.end());

    return static_cast<int>(before - retired_.size());
}

Pad& Device::pad(int bank, int padIndex) noexcept {
    const auto b = std::clamp(bank, 0, kNumBanks - 1);
    const auto p = std::clamp(padIndex, 0, kNumPads - 1);
    return pads_[static_cast<std::size_t>(b * kNumPads + p)];
}

const Pad& Device::pad(int bank, int padIndex) const noexcept {
    const auto b = std::clamp(bank, 0, kNumBanks - 1);
    const auto p = std::clamp(padIndex, 0, kNumPads - 1);
    return pads_[static_cast<std::size_t>(b * kNumPads + p)];
}

void Device::drainCommands() noexcept {
    Command cmd;
    while (commands_.pop(cmd)) {
        switch (cmd.type) {
            case Command::Type::NoteOn: {
                const int padIndex = cmd.intValue;
                if (padIndex < 0 || padIndex >= kNumPads) break;

                const int slot = activeBank_ * kNumPads + padIndex;
                const auto& p  = pads_[static_cast<std::size_t>(slot)];
                if (p.isEmpty()) break;

                // A raw pointer, lock-free to load. It stays valid because
                // VoiceManager publishes a hazard for it at the end of this
                // block, before the message thread can free anything.
                voices_.noteOn(padIndex, p,
                               slots_[static_cast<std::size_t>(p.slotIndex)].load());
                break;
            }

            case Command::Type::NoteOff:
                voices_.noteOff(cmd.intValue);
                break;

            case Command::Type::AllNotesOff:
                voices_.allNotesOff();
                break;

            case Command::Type::SelectBank:
                activeBank_ = std::clamp(cmd.intValue, 0, kNumBanks - 1);
                break;

            case Command::Type::SelectEffect:
                effects_.selectEffect(static_cast<EffectId>(cmd.intValue));
                break;

            case Command::Type::SetPadSettings: {
                const int slot = std::clamp(cmd.intValue, 0, kNumSlots - 1);
                pads_[static_cast<std::size_t>(slot)] = cmd.pad;
                break;
            }

            case Command::Type::StartResample:
                resampler_.start();
                break;

            case Command::Type::StopResample:
                resampler_.stop();
                break;

            case Command::Type::SetQualityMode:
                // Deliberately NOT handled here: setQualityMode() reallocates
                // FIR state. The UI thread calls it directly while audio is
                // suspended.
                break;

            case Command::Type::SetFidelityMode:
                setFidelityMode(static_cast<FidelityMode>(cmd.intValue));
                break;
        }
    }
}

void Device::publishTelemetry(const float* left, const float* right,
                              int numFrames) noexcept {
    Telemetry t {};
    t.activeVoices = voices_.activeVoiceCount();

    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        peakL = std::max(peakL, std::abs(left[i]));
        peakR = std::max(peakR, std::abs(right[i]));
    }
    t.outputPeakL = peakL;
    t.outputPeakR = peakR;

    // Return value ignored on purpose: if the UI has not drained the queue,
    // dropping a frame of meter data is the correct behaviour.
    (void)telemetry_.push(t);
}

void Device::processBlock(const float* inputLeft, const float* inputRight,
                          float* left, float* right, int numFrames,
                          const seq::TransportInfo& transport) noexcept {
    if (left == nullptr || right == nullptr || numFrames <= 0) return;

    // Marks the start of a block. The retirement rule reads this to know when
    // an in-flight block has finished; incrementing it before any slot is read
    // is what makes that reasoning sound.
    blockCounter_.fetch_add(1, std::memory_order_acq_rel);

    // Commands first, so a REC pressed before this block starts the take in
    // this block for either source, and a stop ends it before anything else
    // is written.
    drainCommands();

    // --- host input -----------------------------------------------------------
    //
    // Read before the bus is touched. Most hosts hand the input over in the
    // very buffers we are about to write, so anything below this point would
    // be sampling our own output by accident.
    //
    // What is captured is the RAW input. Colouring it - the effect, the lo-fi
    // stage - is what RESAMPLE is for, and doing it here as well would print
    // the converter twice. Whether the hardware bandlimits on capture, on
    // playback, or both is UNVERIFIED (docs/HARDWARE_FACTS.md, item 9).
    using Source = ResampleRecorder::Source;

    const bool haveInput = inputLeft != nullptr && inputRight != nullptr;
    const bool listening = haveInput && resampler_.isListeningToInput();

    if (listening && resampler_.isRecordingFrom(Source::Input))
        resampler_.write(inputLeft, inputRight, numFrames);

    if (listening) {
        // Sampling standby: the input is heard through the instrument, effect
        // and lo-fi stage included, the way an external source is on the
        // hardware. Pads still play on top of it. Whether the hardware mutes
        // the pads while sampling is UNVERIFIED.
        if (inputLeft  != left)  std::copy_n(inputLeft,  numFrames, left);
        if (inputRight != right) std::copy_n(inputRight, numFrames, right);
    } else {
        std::fill(left,  left  + numFrames, 0.0f);
        std::fill(right, right + numFrames, 0.0f);
    }

    sequencer_.setTransport(transport);
    // TODO(phase-3): drain sequencer events into voice triggers here.

    voices_.render(left, right, numFrames);

    effects_.setControls(ctrl1.load(std::memory_order_relaxed),
                         ctrl2.load(std::memory_order_relaxed),
                         ctrl3.load(std::memory_order_relaxed));
    effects_.process(left, right, numFrames);

    // Lo-fi colouration sits AFTER the effects, matching the hardware's signal
    // path: the whole processed mix goes through the converter, not just the
    // dry sample.
    for (int i = 0; i < numFrames; ++i) {
        left[i]  = quantizerL_.process(decimatorL_.process(left[i]));
        right[i] = quantizerR_.process(decimatorR_.process(right[i]));
    }

    // Resample taps the FINAL output, after effects and after the lo-fi stage.
    // That is what makes stacking work: each pass re-prints the signal through
    // the converter, and the colouration compounds the way it does on hardware.
    // An input take was already written at the top of the block.
    if (resampler_.isRecordingFrom(Source::Output))
        resampler_.write(left, right, numFrames);

    publishTelemetry(left, right, numFrames);
}

}  // namespace sp303
