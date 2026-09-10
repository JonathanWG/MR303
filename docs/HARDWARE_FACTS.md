# Hardware facts: what is known, and what is not

This file is the project's honesty ledger. Every value the DSP depends on is
listed here with its provenance and confidence. Anything marked **UNVERIFIED**
is currently a placeholder in code, and shipping it as "emulation" would be a
false claim.

Update this file as Phase 0 produces answers. The `UNVERIFIED` markers in the
C++ headers point back here.

---

## Confirmed — multiple independent sources agree

| Fact | Value | Source |
|---|---|---|
| Release year | 2001 | Roland/Boss product record |
| Polyphony | 8 voices | Manual, Sound on Sound review |
| Pads | 8, **not** velocity sensitive | SOS review explicitly notes the absence |
| Banks | 4 (A/B/C/D) | Manual |
| Sampling rates | 44.1 / 22.05 / 11.025 kHz | Manual, Sweetwater specs |
| Internal memory | 31 s / 63 s / 3'10" | Manual |
| Memory card | SmartMedia 8–64 MB, 3.3 V | Manual |
| Effects | 26 total (5 direct + 21 MFX) | Manual, SOS |
| Sequencer | ~7,500 events, 32 patterns, 1–99 bars | SOS |
| Tempo range | ~40–200 BPM | Manual |
| Time-stretch range | 50–130 %, per sample | SOS |
| Effect concurrency | **One at a time** | SOS |
| Resample constraint | Cannot resample across banks | SOS |
| BPM display | Computed from sample **length**, not beat detection | SOS |

> Wikipedia describes the pads as velocity sensitive. The Sound on Sound
> reviewer, working with the unit in hand, wished for velocity sensitivity —
> which settles it. The code follows SOS.

---

## UNVERIFIED — placeholders in code, blocking real fidelity

Ordered by how much each one affects the result.

### 1. Interpolation algorithm — **highest impact**

- **Where:** `core/include/sp303/dsp/Interpolator.h`
- **Placeholder:** linear
- **Why it matters:** determines every artefact heard when a sample is
  transposed or varispeeded. Drop-sample, linear and cubic sound obviously
  different from each other.
- **How to settle:** `tools/identify/identify_interpolation.py` — each
  algorithm leaves a distinct aliasing fingerprint.

### 2. Time-stretch: pitch-preserving or varispeed?

- **Where:** `core/include/sp303/voice/Pad.h` (`speedRatio`)
- **Placeholder:** varispeed (speed changes pitch, like vinyl)
- **Why it matters:** if it actually preserves pitch, a granular engine is
  required, with completely different artefacts. This changes the voice
  architecture, so it must be answered before Phase 1 hardens.
- **Conflict:** Sound on Sound calls it "time-stretching… in real time", which
  implies pitch preservation. General descriptions of the workflow suggest
  vinyl-like varispeed. **The sources genuinely disagree.**
- **How to settle:** play a known tone at 50 % and 130 % and measure the
  fundamental. Trivial test, large architectural consequence.

### 3. Anti-alias filtering in Long / Lo-Fi

- **Where:** `core/include/sp303/dsp/Decimator.h` (`filterStrength`)
- **Placeholder:** full filtering (`strength = 1.0`)
- **Why it matters:** decides whether lo-fi modes sound dark and clean or
  metallic and aliased. The device's reputation hints at the latter, but that
  is an inference, not a measurement.
- **How to settle:** `hf_tone` stimulus in Lo-Fi mode; look at where the energy
  lands on output.

### 4. Internal bit depth and rounding behaviour

- **Where:** `core/include/sp303/dsp/Quantizer.h`
- **Placeholder:** 16-bit, round
- **Note:** public sources cite "20-bit converters". Converter resolution does
  **not** determine the internal data path width, and no source states the
  rounding behaviour.
- **How to settle:** `dc_steps` and `fade` stimuli.

### 5. Filter topology and resonance law

- **Where:** `core/include/sp303/fx/FilterDrive.h`
- **Placeholder:** single biquad lowpass, Q capped at ~8.7, no self-oscillation
- **Open questions:** pole count, response type, whether it self-oscillates. If
  it does, that behaviour is part of the sound and must be reproduced, not
  "fixed".
- **How to settle:** swept-sine response over a CTRL 1 × CTRL 2 grid.

### 6. Saturation curve

- **Where:** `core/include/sp303/dsp/Saturator.h`
- **Placeholder:** `tanh`
- **How to settle:** `sines` stimulus (amplitude ladder); fit the measured
  harmonic series.

### 7. Control taper

- **Where:** every effect's `setControls`
- **Placeholder:** exponential for cutoff, linear for the rest
- **Why it matters:** the taper is how the instrument *feels* under the fingers.
  Matching the endpoints while getting the curve wrong still feels wrong.
- **How to settle:** sample the parameter at a grid of knob positions.

### 8. Swing law and available grids

- **Where:** `core/include/sp303/sequencer/Quantize.h`
- **Placeholder:** odd subdivisions delayed, 50–75 % range
- **How to settle:** record a known performance, measure where each event
  actually landed.

### 9. Effect signal-path position

- **Where:** `core/src/Device.cpp` — decimation and quantisation currently sit
  *after* the effects.
- **Reasoning:** the whole processed mix passes through the converter on the
  hardware, not just the dry sample.
- **Status:** plausible, unconfirmed. Resampling through an effect would show
  whether the lo-fi colouration compounds.

### 10. Analogue output stage

- **Where:** not yet modelled
- **How to settle:** deconvolve the sweep capture into an impulse response and
  load it into a short FIR. Cheap, and it measurably improves the null-test.

---

## Historical note

The claim that *Donuts* (2006) was made on an SP-303 is widespread and
imprecise. Most of the album was composed and mixed in Pro Tools before J Dilla
received an SP-404 in hospital. The SP-303 was real in his workflow during that
period, but the popular attribution does not survive scrutiny.

It has no bearing on the DSP. It is recorded here because marketing copy that
repeats the myth would be inaccurate, and accuracy is cheaper than a correction.
