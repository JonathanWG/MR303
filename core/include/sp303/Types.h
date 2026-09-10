#pragma once

#include <cstddef>
#include <cstdint>

namespace sp303 {

// ---------------------------------------------------------------------------
// Hardware constants.
//
// Sources: SP-303 owner's manual, Roland support articles, Sound on Sound
// review. See ../../docs/HARDWARE_FACTS.md for provenance and for the list of
// values that are still UNVERIFIED and must be settled by Phase 0 measurement.
// ---------------------------------------------------------------------------
inline constexpr int kNumPads         = 8;
inline constexpr int kNumBanks        = 4;   // A B C D
inline constexpr int kNumSlots        = kNumPads * kNumBanks;
inline constexpr int kMaxVoices       = 8;   // polyphony
inline constexpr int kMaxPatterns     = 32;
inline constexpr int kMaxPatternBars  = 99;
inline constexpr int kMinPatternBars  = 1;

inline constexpr float kMinBpm        = 40.0f;
inline constexpr float kMaxBpm        = 200.0f;

// Time-stretch / varispeed range, per pad.
inline constexpr float kMinSpeedRatio = 0.50f;
inline constexpr float kMaxSpeedRatio = 1.30f;

// ---------------------------------------------------------------------------
// Sampling quality mode. Drives both the capture rate and the lo-fi character.
// ---------------------------------------------------------------------------
enum class QualityMode : std::uint8_t {
    Standard,  // 44.1 kHz  - 31 s internal
    Long,      // 22.05 kHz - 63 s internal
    LoFi       // 11.025 kHz - 3 min 10 s internal
};

constexpr double sampleRateFor(QualityMode m) noexcept {
    switch (m) {
        case QualityMode::Standard: return 44100.0;
        case QualityMode::Long:     return 22050.0;
        case QualityMode::LoFi:     return 11025.0;
    }
    return 44100.0;
}

// Internal memory budget in seconds (mono, no memory card).
constexpr double internalSecondsFor(QualityMode m) noexcept {
    switch (m) {
        case QualityMode::Standard: return 31.0;
        case QualityMode::Long:     return 63.0;
        case QualityMode::LoFi:     return 190.0;  // 3'10"
    }
    return 31.0;
}

// ---------------------------------------------------------------------------
// Pad playback behaviour. The hardware exposes these as three independent
// toggles, which multiply out to 8 distinct trigger behaviours.
// ---------------------------------------------------------------------------
enum class TriggerMode : std::uint8_t {
    Trigger,  // one press plays through to END regardless of release
    Gate      // plays only while the pad is held
};

enum class LoopMode : std::uint8_t {
    OneShot,
    Loop
};

// ---------------------------------------------------------------------------
// Effects. Five have dedicated buttons; the rest live behind MFX.
// Order is stable and is part of the persisted state - append only.
// ---------------------------------------------------------------------------
enum class EffectId : std::uint8_t {
    None = 0,
    // Direct-access buttons
    FilterDrive,
    Pitch,
    Delay,
    VinylSim,
    Isolator,
    // MFX bank
    Reverb,
    TapeEcho,
    Slicer,
    VoiceTransformer,
    Distortion,
    LoFiFx,
    Compressor,
    Chorus,
    Flanger,
    Phaser,
    // ... remaining MFX algorithms land here as they are measured
    Count
};

// ---------------------------------------------------------------------------
// Authenticity policy.
//
// Authentic reproduces the hardware's constraints (they are a feature of the
// workflow). Modern lifts them. See docs/ARCHITECTURE.md section "Authentic
// vs Modern".
// ---------------------------------------------------------------------------
enum class FidelityMode : std::uint8_t { Authentic, Modern };

}  // namespace sp303
