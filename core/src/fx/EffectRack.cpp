#include "sp303/fx/EffectRack.h"

#include "sp303/fx/Chorus.h"
#include "sp303/fx/Delay.h"
#include "sp303/fx/FilterDrive.h"
#include "sp303/fx/Flanger.h"
#include "sp303/fx/Isolator.h"
#include "sp303/fx/Phaser.h"
#include "sp303/fx/Pitch.h"
#include "sp303/fx/Reverb.h"
#include "sp303/fx/TapeEcho.h"
#include "sp303/fx/VinylSim.h"

namespace sp303::fx {

EffectRack::EffectRack() {
    // Every effect is instantiated here, once, off the audio thread.
    //
    // The five direct-access buttons.
    effects_.push_back(std::make_unique<FilterDrive>());
    effects_.push_back(std::make_unique<Pitch>());
    effects_.push_back(std::make_unique<Delay>());
    effects_.push_back(std::make_unique<VinylSim>());
    effects_.push_back(std::make_unique<Isolator>());

    // The MFX bank, in the order the shared building blocks were written:
    // the two on dsp::DelayLine first, then the three on dsp::Lfo.
    effects_.push_back(std::make_unique<Reverb>());
    effects_.push_back(std::make_unique<TapeEcho>());
    effects_.push_back(std::make_unique<Chorus>());
    effects_.push_back(std::make_unique<Flanger>());
    effects_.push_back(std::make_unique<Phaser>());

    // TODO(phase-4): the remaining MFX algorithms land here as they are
    // written - Slicer, Voice Transformer, Distortion, Lo-Fi, Compressor and
    // the rest of the 21. Build order is by family (see
    // SP-303_Plugin_Plano_Engenharia.md section 8.5) so shared building
    // blocks - delay line, modulator, pitch engine - are written once.
}

bool EffectRack::isAvailable(EffectId id) const noexcept {
    for (const auto& e : effects_)
        if (e->id() == id) return true;
    return false;
}

void EffectRack::prepare(double sampleRate, int maxBlockSize) {
    for (auto& e : effects_)
        e->prepare(sampleRate, maxBlockSize);

    // 20 ms ramp: fast enough to feel immediate under the fingers, slow enough
    // that a hard knob sweep does not produce steps.
    ctrl1_.prepare(sampleRate, 20.0);
    ctrl2_.prepare(sampleRate, 20.0);
    ctrl3_.prepare(sampleRate, 20.0);

    ctrl1_.reset(0.5f);
    ctrl2_.reset(0.0f);
    ctrl3_.reset(0.0f);
}

void EffectRack::reset() noexcept {
    for (auto& e : effects_) e->reset();
}

IEffect* EffectRack::current() noexcept {
    for (auto& e : effects_)
        if (e->id() == currentId_) return e.get();
    return nullptr;
}

void EffectRack::selectEffect(EffectId id) noexcept {
    if (id == currentId_) return;
    currentId_ = id;

    // Clear state so a delay tail or filter resonance from the previous effect
    // does not bleed into the new one.
    if (auto* e = current()) e->reset();
}

void EffectRack::setControls(float ctrl1, float ctrl2, float ctrl3) noexcept {
    ctrl1_.setTarget(ctrl1);
    ctrl2_.setTarget(ctrl2);
    ctrl3_.setTarget(ctrl3);
}

void EffectRack::process(float* left, float* right, int numFrames) noexcept {
    auto* effect = current();
    if (effect == nullptr || currentId_ == EffectId::None) return;

    // Knobs are applied per block rather than per sample. At a 20 ms ramp and
    // typical block sizes this is inaudible, and it keeps the per-sample path
    // free of three smoother updates.
    //
    // If a future effect turns out to need per-sample control resolution
    // (an audio-rate-modulated filter, say), that effect can pull from the
    // smoothers directly rather than changing this contract for everyone.
    for (int i = 0; i < numFrames; ++i) {
        ctrl1_.next();
        ctrl2_.next();
        ctrl3_.next();
    }

    effect->setControls(ctrl1_.current(), ctrl2_.current(), ctrl3_.current());
    effect->process(left, right, numFrames);
}

const char* EffectRack::ctrl1Name() const noexcept {
    for (const auto& e : effects_)
        if (e->id() == currentId_) return e->ctrl1Name();
    return "-";
}

const char* EffectRack::ctrl2Name() const noexcept {
    for (const auto& e : effects_)
        if (e->id() == currentId_) return e->ctrl2Name();
    return "-";
}

const char* EffectRack::ctrl3Name() const noexcept {
    for (const auto& e : effects_)
        if (e->id() == currentId_) return e->ctrl3Name();
    return "-";
}

}  // namespace sp303::fx
