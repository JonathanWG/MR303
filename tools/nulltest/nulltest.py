#!/usr/bin/env python3
"""Null-test harness: how close is the emulation to the hardware?

THIS IS THE MOST IMPORTANT TOOL IN THE REPOSITORY.

It is what turns "we think it sounds close" into a number that cannot silently
get worse. Run it in CI and fail the build when the residual regresses.

Method
------
1. Take a reference recording captured from the real hardware (Phase 0).
2. Take the plugin's render of the same source material.
3. Time-align them (the hardware has latency the plugin does not).
4. Gain-match (small level differences would otherwise dominate the residual).
5. Invert one, sum, and measure what is left.

Interpreting the residual
-------------------------
    > -20 dB   nowhere near - the algorithm is probably wrong
    -20..-40   "in the ballpark", audibly different under A/B
    -40..-60   very close - typical target for a behavioural emulation
    < -60 dB   excellent
    < -90 dB   effectively bit-exact (only reachable on a pure digital path)

A poor residual is not a reason to loosen the threshold. It is a reason to go
back to the measurement and find out what the algorithm is actually doing.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, asdict
from pathlib import Path

import numpy as np
import soundfile as sf


@dataclass
class NullResult:
    name: str
    residual_db: float
    reference_rms_db: float
    delay_samples: int
    gain_correction_db: float
    passed: bool
    threshold_db: float

    def summary(self) -> str:
        mark = "PASS" if self.passed else "FAIL"
        return (f"[{mark}] {self.name:<28} residual {self.residual_db:7.2f} dB "
                f"(threshold {self.threshold_db:.1f})  "
                f"delay {self.delay_samples:>5} smp  "
                f"gain {self.gain_correction_db:+.2f} dB")


def to_mono(x: np.ndarray) -> np.ndarray:
    return x if x.ndim == 1 else x.mean(axis=1)


def db(x: float) -> float:
    return 20.0 * np.log10(max(x, 1e-12))


def find_delay(reference: np.ndarray, test: np.ndarray, max_lag: int = 96000) -> int:
    """Cross-correlate to recover the hardware's extra latency.

    Without this step every result is garbage: a handful of samples of offset
    turns a perfect match into a residual near 0 dB.
    """
    n = min(len(reference), len(test))
    ref = reference[:n] - reference[:n].mean()
    tst = test[:n] - test[:n].mean()

    size = 1 << int(np.ceil(np.log2(2 * n)))
    corr = np.fft.irfft(np.fft.rfft(tst, size) * np.conj(np.fft.rfft(ref, size)), size)

    lag = min(max_lag, n - 1)
    search = np.concatenate([corr[-lag:], corr[:lag + 1]])
    return int(np.argmax(search) - lag)


def align(reference: np.ndarray, test: np.ndarray, delay: int) -> tuple[np.ndarray, np.ndarray]:
    if delay > 0:
        test = test[delay:]
    elif delay < 0:
        reference = reference[-delay:]
    n = min(len(reference), len(test))
    return reference[:n], test[:n]


def match_gain(reference: np.ndarray, test: np.ndarray) -> tuple[np.ndarray, float]:
    """Least-squares scalar that best fits `test` onto `reference`."""
    denom = float(np.dot(test, test))
    if denom <= 0.0:
        return test, 0.0
    scale = float(np.dot(reference, test)) / denom
    return test * scale, db(abs(scale))


def null_test(reference_path: Path, test_path: Path, threshold_db: float) -> NullResult:
    reference, sr_ref = sf.read(str(reference_path), always_2d=False)
    test, sr_test = sf.read(str(test_path), always_2d=False)

    if sr_ref != sr_test:
        raise ValueError(
            f"sample rate mismatch: {reference_path.name} is {sr_ref} Hz, "
            f"{test_path.name} is {sr_test} Hz. Re-render at a matching rate "
            f"rather than resampling here - resampling would add its own "
            f"artefacts and contaminate the measurement."
        )

    reference = to_mono(np.asarray(reference, dtype=np.float64))
    test = to_mono(np.asarray(test, dtype=np.float64))

    delay = find_delay(reference, test)
    reference, test = align(reference, test, delay)
    test, gain_db = match_gain(reference, test)

    residual = reference - test
    residual_rms = float(np.sqrt(np.mean(residual ** 2)))
    reference_rms = float(np.sqrt(np.mean(reference ** 2)))

    # Report the residual RELATIVE to the reference. An absolute figure would
    # flatter quiet material and punish loud material for no good reason.
    relative_db = db(residual_rms) - db(reference_rms)

    return NullResult(
        name=test_path.stem,
        residual_db=relative_db,
        reference_rms_db=db(reference_rms),
        delay_samples=delay,
        gain_correction_db=gain_db,
        passed=relative_db <= threshold_db,
        threshold_db=threshold_db,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--reference-dir", type=Path, required=True,
                        help="hardware captures from Phase 0")
    parser.add_argument("--test-dir", type=Path, required=True,
                        help="plugin renders of the same material")
    parser.add_argument("--threshold-db", type=float, default=-40.0,
                        help="maximum acceptable residual (default: -40)")
    parser.add_argument("--json", type=Path, help="write results as JSON")
    args = parser.parse_args()

    references = sorted(args.reference_dir.glob("*.wav"))
    if not references:
        print(f"No reference captures in {args.reference_dir}.", file=sys.stderr)
        print("Phase 0 has not been run yet - see docs/PHASE0.md.", file=sys.stderr)
        return 2

    results: list[NullResult] = []
    missing: list[str] = []

    for reference in references:
        test = args.test_dir / reference.name
        if not test.exists():
            missing.append(reference.name)
            continue
        results.append(null_test(reference, test, args.threshold_db))

    print(f"\nNull-test: {len(results)} comparisons, threshold {args.threshold_db:.1f} dB\n")
    for result in sorted(results, key=lambda r: r.residual_db, reverse=True):
        print("  " + result.summary())

    if missing:
        print(f"\n  {len(missing)} reference(s) had no matching render:")
        for name in missing:
            print(f"    - {name}")

    failed = [r for r in results if not r.passed]
    print(f"\n  {len(results) - len(failed)} passed, {len(failed)} failed, "
          f"{len(missing)} missing\n")

    if args.json:
        args.json.write_text(json.dumps([asdict(r) for r in results], indent=2))

    # Missing renders count as failure: silently skipping a comparison is how
    # coverage quietly rots.
    return 1 if (failed or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
