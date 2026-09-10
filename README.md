# sp303

Sampler engine and audio plugin in the spirit of the Roland/Boss SP-303 "Dr. Sample".

> **Status: scaffold, and it builds.** The architecture, threading model and
> build system are in place, the core DSP primitives are implemented and
> unit-tested, and as of 26/08/2026 the whole thing compiles and the test suite
> runs green (57/57 on MSVC 19.44). The sound is **not** yet matched to the
> hardware — every algorithm marked `UNVERIFIED` in the headers is a placeholder
> awaiting Phase 0 measurement. Do not describe this as an emulation until the
> null-test passes.

Background research: [`docs/research/SP-303_Estudo_Completo.md`](docs/research/SP-303_Estudo_Completo.md)
· Engineering plan: [`docs/research/SP-303_Plugin_Plano_Engenharia.md`](docs/research/SP-303_Plugin_Plano_Engenharia.md)

---

## Layout

```
core/          Layer 0 — pure C++, zero dependencies. The actual instrument.
  include/     Public headers
  src/         Implementation
  tests/       Catch2 unit tests
plugin/        Layers 1–4 — JUCE wrapper (VST3 / AU / Standalone)
  src/Skin     Panel loader: artwork + hitbox map
  src/Panel…   The interactive panel — pads, knobs, overlays
tools/         Python — measurement, identification, null-test, skin builder
docs/          Architecture, hardware facts, Phase 0 protocol, licensing
resources/     Panel skins, factory content
```

## Panel skins

The GUI is a **background image plus a map of where every control sits**. Both
are generated from one layout definition, so they cannot drift apart:

```bash
python tools/skin/build_skin.py      # emits panel.svg + panel.json
python tools/skin/validate_skin.py   # overlaps, off-canvas, dead controls
```

Anything the artwork cannot know — which pad is lit, where a knob is actually
pointing, what the CTRL knobs currently do — is drawn as an overlay on top. That
is what lets the same code drive the default vector panel today and a photograph
of real hardware tomorrow.

**To use a photograph:** shoot the unit square-on with even lighting, drop it in
as `panel.png`, point `"image"` at it, and re-measure the hitboxes against the
photo's pixel size. No C++ changes.

> A product photograph belongs to whoever took it, and a hardware panel's
> appearance can be protected trade dress. Shooting your own unit resolves the
> first. See [`docs/LICENSING.md`](docs/LICENSING.md) for the second.

**`core/` does not depend on JUCE.** That is deliberate: it can be unit-tested
headless, driven by the Python harness without a DAW, and swapped to a different
plugin framework without touching a line of DSP.

## Build

Requires CMake 3.22+ and a C++20 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Plugin (fetches JUCE on first configure — see [`docs/LICENSING.md`](docs/LICENSING.md) first):

```bash
cmake -B build -DSP303_BUILD_PLUGIN=ON && cmake --build build --parallel
```

Real-time safety checks (clang 20+):

```bash
cmake -B build-rt -DCMAKE_CXX_COMPILER=clang++ -DSP303_RT_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
```

Python tooling:

```bash
pip install -r tools/requirements.txt
```

## The one rule

Anything reachable from `Device::processBlock()` runs on a real-time thread with
a deadline of roughly 2.7 ms. It must never allocate, lock, or touch the disk.

Mark such functions `SP303_RT` (see `core/include/sp303/rt/RtSafe.h`). Under
clang with `SP303_RT_SANITIZE=ON` that becomes an enforced contract rather than
a comment, and CI runs that build.

Communication with the audio thread goes through exactly two mechanisms:

| Kind of data | Mechanism |
|---|---|
| Discrete commands (note on, load pad, change mode) | `rt::SpscQueue<Command>` |
| Continuous parameters (CTRL 1/2/3) | `std::atomic<float>` + `ParameterSmoother` |
| Sample audio | `SampleSlot` — atomic raw-pointer publish + hazard-pointer reclamation |
| Telemetry back to the UI | `rt::SpscQueue<Telemetry>`, lossy by design |

## What works today

- 8-voice polyphonic playback with all trigger modes (Trigger/Gate × Loop/OneShot × Normal/Reverse × Hold)
- START / END / LEVEL per pad, 4 banks of 8
- Lock-free command and telemetry queues, verified under concurrent load
- Interpolation (drop-sample / linear / cubic), biquad filters, saturation, quantisation, decimation
- All five direct-access effects: Filter+Drive, Pitch, Delay, Vinyl Sim, Isolator
- Resample capture, wired to the panel: RESAMPLE → pad → REC → REC
- Swing quantisation maths
- Skin-driven panel: clickable pads, draggable knobs, drag-and-drop onto pads,
  computer-keyboard pad triggering, live overlays, resizable with locked aspect
- MIDI pad triggering and host-automatable CTRL knobs
- 91 unit tests + skin geometry validation

## What does not

- **Nothing is matched to hardware yet** — Phase 0 has not run. This is the one
  that decides whether the project is an emulation or a good lo-fi sampler.
- 21 of the 26 effects (the MFX bank)
- Pattern sequencer playback (types exist; `processBlock` is an honest no-op)
- Sampling from the plugin's audio input — REC only exists inside the resample
  flow today
- MARK / DEL / ST-END / TIME-BPM / REMAIN and the playback toggles are drawn and
  hit-tested but inert — `PanelComponent::performAction` falls through to a
  status message

Compiles and runs, but with no versioned regression coverage yet:

- State persistence — pad settings plus FLAC-embedded audio, via `ValueTree`
  binary stream in `get/setStateInformation`

## Next step

Add sampling from the plugin's audio input, then start the MFX bank.

Separately, and on its own clock: **access to a physical SP-303** to run
[`docs/PHASE0.md`](docs/PHASE0.md). Without it the ceiling is a good lo-fi
sampler rather than a verified emulation — a fine outcome, but it should be a
conscious choice rather than a discovery made six months in.

## Legal

Do **not** dump, disassemble or port Roland firmware. Black-box behavioural
measurement only. Do not use Roland/Boss trademarks or copy the panel's visual
identity. See [`docs/LICENSING.md`](docs/LICENSING.md) — these are blocking
items, not paperwork for later.
