#!/usr/bin/env python3
"""Phase 0: generate the test signals and drive the hardware capture session.

Read docs/PHASE0.md before running this.

The point of Phase 0 is to replace guesses with measurements. Everything marked
UNVERIFIED in the C++ headers is settled here, and nothing downstream is worth
building until it is.

Signal set and what each one recovers
-------------------------------------
  sweep        log sine sweep 20 Hz - 20 kHz : frequency response, and the
               impulse response by deconvolution
  impulse      single-sample click            : latency, output stage ringing
  white        white noise                    : noise floor, spectral tilt
  sines        discrete tones at many levels  : saturation curve, THD
  dc_steps     DC staircase                   : quantisation behaviour, dither
  hf_tone      tone near Nyquist              : anti-alias filter strength in
                                                Long and Lo-Fi modes
  fade         slow fade to silence           : truncate vs round vs dither

Capture chain
-------------
  PC -> interface line out -> SP-303 INPUT -> SP-303 LINE OUT -> interface in

Record at 96 kHz / 24 bit. The extra headroom above the device's own rate is
what makes aliasing products visible instead of already folded down.

Keep the input level identical across every take. Level differences show up in
the null-test as a residual floor that no amount of algorithm work can remove.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf

SAMPLE_RATE = 96000


def log_sweep(duration: float = 10.0, f0: float = 20.0, f1: float = 20000.0) -> np.ndarray:
    """Exponential sine sweep - deconvolves to an impulse response, and
    separates harmonic distortion into distinct pre-arrivals."""
    n = int(duration * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    ratio = np.log(f1 / f0)
    phase = 2 * np.pi * f0 * duration / ratio * (np.exp(t * ratio / duration) - 1.0)
    sweep = np.sin(phase)

    fade = int(0.01 * SAMPLE_RATE)
    sweep[:fade] *= np.linspace(0.0, 1.0, fade)
    sweep[-fade:] *= np.linspace(1.0, 0.0, fade)
    return sweep * 0.5


def impulse(duration: float = 1.0) -> np.ndarray:
    x = np.zeros(int(duration * SAMPLE_RATE))
    x[SAMPLE_RATE // 10] = 0.9
    return x


def white_noise(duration: float = 10.0, seed: int = 1) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return rng.standard_normal(int(duration * SAMPLE_RATE)) * 0.2


def sine_ladder(frequency: float = 1000.0, seconds_per_step: float = 1.0) -> np.ndarray:
    """One tone at many amplitudes. Fitting the harmonic series that comes back
    out identifies the DRIVE waveshaping curve."""
    levels_db = [-40, -30, -24, -18, -12, -9, -6, -3, 0]
    blocks = []
    for level_db in levels_db:
        n = int(seconds_per_step * SAMPLE_RATE)
        t = np.arange(n) / SAMPLE_RATE
        amplitude = 10 ** (level_db / 20.0)
        blocks.append(np.sin(2 * np.pi * frequency * t) * amplitude)
        blocks.append(np.zeros(int(0.2 * SAMPLE_RATE)))
    return np.concatenate(blocks)


def dc_staircase(steps: int = 64, seconds_per_step: float = 0.25) -> np.ndarray:
    """Reveals the quantisation grid directly: each tread lands on whatever
    level the converter can actually represent."""
    blocks = [np.full(int(seconds_per_step * SAMPLE_RATE), v)
              for v in np.linspace(-0.9, 0.9, steps)]
    return np.concatenate(blocks)


def hf_tone(frequency: float = 15000.0, duration: float = 5.0) -> np.ndarray:
    """In Lo-Fi mode (11.025 kHz) this is well above Nyquist. Where its energy
    lands on the way out tells you how much anti-aliasing is really happening."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE
    return np.sin(2 * np.pi * frequency * t) * 0.5


def slow_fade(duration: float = 15.0, frequency: float = 440.0) -> np.ndarray:
    """Truncation, rounding and dither diverge most audibly in the last few
    bits of a fade."""
    n = int(duration * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    envelope = np.exp(-np.linspace(0.0, 12.0, n))
    return np.sin(2 * np.pi * frequency * t) * envelope * 0.9


SIGNALS = {
    "sweep":    log_sweep,
    "impulse":  impulse,
    "white":    white_noise,
    "sines":    sine_ladder,
    "dc_steps": dc_staircase,
    "hf_tone":  hf_tone,
    "fade":     slow_fade,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=Path("captures/stimuli"))
    parser.add_argument("--only", nargs="*", choices=sorted(SIGNALS),
                        help="generate a subset")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    names = args.only or sorted(SIGNALS)

    for name in names:
        signal = SIGNALS[name]()
        path = args.out / f"{name}.wav"
        sf.write(str(path), signal, SAMPLE_RATE, subtype="PCM_24")
        print(f"  wrote {path}  ({len(signal) / SAMPLE_RATE:.1f}s)")

    print(f"\n{len(names)} stimulus file(s) in {args.out}\n")
    print("Next: play each through the SP-303 and capture LINE OUT.")
    print("Repeat the full set for EVERY combination of:")
    print("  quality mode  : Standard, Long, Lo-Fi")
    print("  effect        : bypass, then each effect over a CTRL grid")
    print("  pitch/speed   : the full transposition range")
    print("\nName captures to match the stimulus so nulltest.py can pair them.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
