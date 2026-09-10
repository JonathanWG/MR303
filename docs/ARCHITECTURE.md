# Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────┐
│  4  FORMAT WRAPPERS         VST3 · AU · CLAP · Standalone    │
├──────────────────────────────────────────────────────────────┤
│  3  GUI / EDITOR            message thread, ~30–60 fps       │
├──────────────────────────────────────────────────────────────┤
│  2  CONTROLLER / STATE      parameters · presets · undo      │
├──────────────────────────────────────────────────────────────┤
│  1  ENGINE (real-time)      voices · sequencer · fx · resample│
├──────────────────────────────────────────────────────────────┤
│  0  DSP CORE (pure C++)     interpolation · filters · 26 fx  │
└──────────────────────────────────────────────────────────────┘
```

Layer 0 has **no dependency on JUCE or any plugin SDK**. Three payoffs:

1. Unit tests run headless in milliseconds.
2. The Python null-test harness can drive it without a DAW in the loop.
3. Switching plugin framework never touches a line of DSP.

`Device` is the seam. It knows nothing about hosts; `Sp303AudioProcessor`
translates between the host's world and it, and holds almost no logic itself.

---

## Threading

```
  AUDIO THREAD                MESSAGE THREAD              BACKGROUND WORKER
  (real-time priority)        (~30–60 fps)                (pool)
  ────────────────────        ──────────────              ─────────────────
  processBlock()              repaint, input              decode audio files
   · drainCommands()          · read telemetry            · sample-rate convert
   · render voices            · write commands            · offline resample
   · run effect               · draw meters               · build sample banks
   · lo-fi stage
   · publishTelemetry()
        │                            │                            │
        │ ◄── SpscQueue<Command> ────┤                            │
        ├──── SpscQueue<Telemetry> ─►│                            │
        │                                                         │
        └──── SampleSlot: atomic raw-pointer publish ◄────────────┘
```

### The four channels, and why each is what it is

**Discrete commands — `SpscQueue<Command>`.** Ordering matters (a NoteOff must
not overtake its NoteOn) and dropping one is a real bug — a lost NoteOff leaves
a voice stuck on. `push` returns `false` when full so the caller sees it rather
than losing it silently.

**Continuous parameters — `atomic<float>` + `ParameterSmoother`.** Knobs go
through a plain atomic, *not* the queue. During a fast sweep the audio thread
wants the newest value, not a backlog of stale ones. `ParameterSmoother` ramps
over ~20 ms, which is what prevents zipper noise — an artefact of our
implementation that the hardware does not have.

**Sample audio — `SampleSlot` + hazard pointers.** `SampleBuffer` is immutable
once constructed. The loader thread builds a whole new one and publishes a raw
pointer to it; the audio thread reads that pointer, which is lock-free on every
target.

It used to be an atomic `shared_ptr`, and that was wrong. Measured on
MSVC 19.44 / x64, `std::atomic<std::shared_ptr<T>>::is_lock_free()` is **false**
— the implementation falls back to an internal mutex, so `load()` blocked the
audio thread on every pad hit. The same design also ran the last reference drop,
and therefore `operator delete`, on the audio thread whenever a voice ended.

Raw pointers buy lock-freedom at the cost of manual lifetime, so reclamation is
explicit:

```
  message thread                    audio thread
  ──────────────                    ────────────
  slot.publish(new) ─────────────►  slot.load()  (raw, lock-free)
       │ returns the old one              │
       ▼                                  ▼
  retired_.push_back({old, block})   Voice holds the raw pointer
       │                                  │
       │                                  ▼
       │                          VoiceManager::publishHazards()
       │                          at the end of every block
       ▼                                  │
  collectRetiredSamples()  ◄──────────────┘
    frees `old` only when
      · the block it was retired in has ended, AND
      · no hazard still names it
```

The first condition covers the narrow window where a block has loaded a pointer
but has not yet handed it to a voice; the second covers a note that outlives any
number of blocks. Freeing happens on the message thread, driven by the
processor's 20 Hz timer.

If the audio thread stops entirely the block counter stops moving and nothing is
freed — a bounded leak while stopped, which is the safe side to err on.
`allNotesOff()` clears the hazards, so the next block releases everything.

**Telemetry — `SpscQueue<Telemetry>`, lossy.** Meter and LED data is stale the
moment it is produced. The UI drains the whole queue and keeps only the last
frame; a failed `push` is ignored on purpose.

---

## Signal path

```
  pads ──► VoiceManager (8 voices, interpolated read)
                │
                ▼
           EffectRack (exactly one effect active in Authentic mode)
                │
                ▼
        Decimator ──► Quantizer          ← the lo-fi colouration
                │
                ▼
             output ──┬──► host
                      └──► resample capture buffer
```

The lo-fi stage sits **after** the effects: on the hardware the whole processed
mix passes through the converter, not just the dry sample. That ordering is
plausible but unconfirmed — see `HARDWARE_FACTS.md` item 9.

The tap into the resample buffer is what makes the instrument's defining loop
work: sample → chop → filter → **resample** → filter again. One effect at a
time is not a limitation to engineer around; it is what forces that workflow.

---

## Authentic vs Modern

A single toggle that resolves the fidelity-versus-usability tension without
compromising either.

| | Authentic | Modern |
|---|---|---|
| Memory limit | 31 s / 63 s / 3'10" | Unlimited |
| Slots | 32 | Unlimited banks |
| Simultaneous effects | 1 (forces resampling) | Free chain |
| Pads | No velocity | MIDI velocity honoured |
| START/END editing | By ear, no waveform | Waveform + zoom |
| Quantise | Destructive on record | Non-destructive, undoable |
| Resample | Real-time, same bank only | Also offline, across banks |

This is not an "easy mode". The hardware's constraints are a feature for anyone
who wants the original workflow, and gratuitous friction for anyone who does
not. Host automation of CTRL 1/2/3 is exposed in **both** modes: withholding it
would cost users something real and buy no sonic fidelity.

---

## Host integration

**Transport.** `readTransport()` detects scrubs and loop wraps by watching for
backward or large forward jumps in PPQ. Without that the sequencer integrates a
jump as elapsed musical time and fires a burst of stale events.

**MIDI.** Pads map to 8 consecutive notes from C1 (36). The hardware has no
MIDI note input for pads at all — this is a necessary addition for controllers
and MIDI clips.

**Sampling into the plugin.** Three paths, all worth supporting: the track's own
input (closest to hardware), a sidechain input (more flexible in Live), and
drag-and-drop (what most users will actually use).

**State.** Pad audio should be embedded in the project by default, with
file-reference as an option. Projects broken by a moved sample are the number
one complaint about sampler plugins, and portability is worth the file size.

---

## Conventions

- Everything reachable from `processBlock()` is marked `SP303_RT`.
- `prepare()` may allocate. `process()` may not. No exceptions.
- Placeholders carry an `UNVERIFIED` comment pointing at `HARDWARE_FACTS.md`.
- Unimplemented features are honest no-ops with a `TODO(phase-N)` — a sequencer
  that fires events at approximately the right time is worse than one that is
  visibly switched off.
