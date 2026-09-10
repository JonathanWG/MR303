# Phase 0 — Measurement protocol

**Nothing downstream is worth building until this is done.**

Every algorithm in `core/` marked `UNVERIFIED` is currently a guess. Phase 0
replaces guesses with measurements, and produces the reference dataset that the
null-test gate compares against forever after.

Estimated effort: **3–4 weeks**.

---

## Prerequisite

A physical SP-303, and an audio interface capturing at **96 kHz / 24-bit**.

Recording above the device's own rates is what makes aliasing products visible
instead of already folded down.

If the hardware cannot be obtained, say so out loud and re-scope: the ceiling
becomes a generic lo-fi effect. That should be a decision, not something
discovered six months in.

---

## Signal chain

```
  PC  ──► interface line out ──► SP-303 INPUT
                                     │
  PC  ◄── interface line in  ◄── SP-303 LINE OUT
```

Rules that matter more than they look:

1. **Never change the input level between takes.** Level differences become a
   residual floor in the null-test that no algorithm work can remove.
2. **Record a loopback reference** (interface out straight back to interface in)
   so the interface's own contribution can be deconvolved out.
3. **Log everything** — every knob position, every mode. An unlabelled capture
   is a wasted capture.

---

## Step 1 — Generate stimuli

```bash
pip install -r tools/requirements.txt
python tools/measure/capture.py --out captures/stimuli
```

| Stimulus | Recovers |
|---|---|
| `sweep` | Frequency response; impulse response by deconvolution |
| `impulse` | Latency, output-stage ringing |
| `white` | Noise floor, spectral tilt |
| `sines` | Saturation curve, THD vs level |
| `dc_steps` | Quantisation grid |
| `hf_tone` | Anti-alias filter strength in Long / Lo-Fi |
| `fade` | Truncate vs round vs dither |

## Step 2 — Capture matrix

Run the full stimulus set through each condition:

**A. Dry path** — 3 quality modes × mono/stereo = 6 passes.
Establishes the baseline: response, noise floor, latency, output stage.

**B. Transposition** — the full pitch/speed range in steps.
Settles interpolation (item 1) and the varispeed-vs-time-stretch question
(item 2). Do this one early: item 2 changes the voice architecture, so it must
be answered before Phase 1 hardens.

**C. Effects** — for each of the 26, sweep CTRL 1/2/3 on a grid (8×8×8 is a
reasonable start; refine where the response moves fastest).
This is the bulk of the capture time. Automate what MIDI allows.

**D. Sequencer timing** — record known performances against the metronome at
several quantise and swing settings, then measure where each event actually
landed. Recovers the real swing law (item 8).

## Step 3 — Identify

```bash
python tools/identify/identify_interpolation.py \
    --capture captures/hw/pitch_ratio_1p37.wav \
    --source  captures/stimuli/sine_1k.wav \
    --ratio   1.37
```

Work through `HARDWARE_FACTS.md` in order. Each answer replaces a placeholder
constant, not a block of code — the abstractions were built so measurement
results drop in as parameters.

## Step 4 — Reference dataset

Commit the aligned captures to `captures/reference/`, then enable the
`null-test` job in `.github/workflows/ci.yml` (currently `if: false`).

From that point the residual is a number that cannot silently get worse.

---

## Reading the null-test

| Residual | Meaning |
|---|---|
| > −20 dB | Algorithm is probably wrong |
| −20 to −40 | Ballpark; audibly different under A/B |
| −40 to −60 | Very close — the target for behavioural emulation |
| < −60 dB | Excellent |
| < −90 dB | Effectively bit-exact |

A poor residual is never a reason to loosen the threshold. It is a reason to go
back and find out what the algorithm is actually doing.

**Why bit-exact is even conceivable here:** unlike an analogue synth, the
SP-303's core is digital end to end — conversion, decimation, filtering,
effects, sequencing are all DSP. Only the input preamp and output stage are
analogue. The digital path can in principle be matched exactly; the analogue
ends get modelled from measured impulse responses.

---

## Legal boundary

This is **black-box behavioural** reverse engineering: measure inputs and
outputs, infer behaviour, reimplement independently.

Do **not** dump, disassemble, or port Roland firmware — that is a different
legal category with a very different risk profile.

Keep the capture logs and this protocol. Documented clean-room provenance is
what makes the work defensible.
