#!/usr/bin/env python3
"""Identify which interpolation algorithm the hardware uses.

This settles the single most consequential UNVERIFIED question in the project
(see core/include/sp303/dsp/Interpolator.h).

How it works
------------
Play a pure sine through the sampler at a transposition ratio that is not a
simple fraction. Each interpolation algorithm leaves a different fingerprint in
the spectrum:

  drop-sample  strong images at k*fs +/- f, falling off slowly (a zero-order
               hold is a very poor lowpass) - the brightest, grittiest result
  linear       same image structure but rolled off much harder; a distinctive
               sinc^2 tilt across the top end
  cubic        images suppressed far below either of the above

The script renders all three candidates locally, then reports which one
correlates best with the hardware capture's spectrum.

Usage
-----
    python identify_interpolation.py \
        --capture captures/hw/pitch_ratio_1p37.wav \
        --source  captures/stimuli/sine_1k.wav \
        --ratio   1.37
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf


def resample_drop(x: np.ndarray, ratio: float) -> np.ndarray:
    n = int(len(x) / ratio)
    idx = np.floor(np.arange(n) * ratio).astype(int)
    idx = np.clip(idx, 0, len(x) - 1)
    return x[idx]


def resample_linear(x: np.ndarray, ratio: float) -> np.ndarray:
    n = int(len(x) / ratio)
    pos = np.arange(n) * ratio
    i = np.floor(pos).astype(int)
    f = pos - i
    i0 = np.clip(i, 0, len(x) - 1)
    i1 = np.clip(i + 1, 0, len(x) - 1)
    return x[i0] * (1.0 - f) + x[i1] * f


def resample_cubic(x: np.ndarray, ratio: float) -> np.ndarray:
    n = int(len(x) / ratio)
    pos = np.arange(n) * ratio
    i = np.floor(pos).astype(int)
    f = (pos - i)[:, None]

    idx = np.stack([np.clip(i + k, 0, len(x) - 1) for k in (-1, 0, 1, 2)], axis=1)
    p = x[idx]

    a = -0.5 * p[:, 0] + 1.5 * p[:, 1] - 1.5 * p[:, 2] + 0.5 * p[:, 3]
    b = p[:, 0] - 2.5 * p[:, 1] + 2.0 * p[:, 2] - 0.5 * p[:, 3]
    c = -0.5 * p[:, 0] + 0.5 * p[:, 2]
    f = f[:, 0]
    return ((a * f + b) * f + c) * f + p[:, 1]


CANDIDATES = {
    "drop-sample": resample_drop,
    "linear":      resample_linear,
    "cubic":       resample_cubic,
}


def spectrum_db(x: np.ndarray, size: int = 1 << 15) -> np.ndarray:
    x = x[:size] if len(x) >= size else np.pad(x, (0, size - len(x)))
    window = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * window))
    return 20.0 * np.log10(np.maximum(mag, 1e-12))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--capture", type=Path, required=True,
                        help="hardware recording of the transposed sample")
    parser.add_argument("--source", type=Path, required=True,
                        help="the original untransposed sample")
    parser.add_argument("--ratio", type=float, required=True,
                        help="transposition ratio used on the hardware")
    args = parser.parse_args()

    capture, _ = sf.read(str(args.capture), always_2d=False)
    source, _ = sf.read(str(args.source), always_2d=False)

    capture = capture if capture.ndim == 1 else capture.mean(axis=1)
    source = source if source.ndim == 1 else source.mean(axis=1)

    target = spectrum_db(capture)

    print(f"\nInterpolation identification  (ratio {args.ratio})\n")
    scores: dict[str, float] = {}

    for name, fn in CANDIDATES.items():
        candidate = spectrum_db(fn(source, args.ratio))
        n = min(len(candidate), len(target))

        # Compare only the top half of the spectrum: that is where the
        # algorithms differ. Below that they are nearly identical and would
        # wash out the discrimination.
        lo = n // 2
        error = float(np.sqrt(np.mean((candidate[lo:n] - target[lo:n]) ** 2)))
        scores[name] = error
        print(f"  {name:<14} spectral RMS error {error:8.3f} dB")

    best = min(scores, key=scores.get)
    print(f"\n  Best match: {best}\n")

    ranked = sorted(scores.values())
    if len(ranked) > 1 and (ranked[1] - ranked[0]) < 1.0:
        print("  WARNING: the top two candidates are within 1 dB. That is not a")
        print("  conclusive result. Re-run with a higher transposition ratio and")
        print("  a source with more high-frequency content before deciding.\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
