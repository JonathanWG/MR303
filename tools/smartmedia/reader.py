#!/usr/bin/env python3
"""Read a SmartMedia card image from a real SP-303 and extract its banks.

STATUS: STUB. The on-card layout has not been reverse engineered yet.

Why this is worth building
--------------------------
Anyone who owns the hardware has years of work sitting on cards. Importing it
directly is a genuine differentiator, and it costs far less than it looks.

Legally this is clean: reading a data format is not the same as copying
firmware. It stays clean as long as nobody dumps or ports Roland's code - see
docs/LICENSING.md.

How to work it out
------------------
1. Format a card in the hardware, sample one known tone onto pad 1, dump the
   raw card image.
2. Repeat with a different tone, and with the tone on a different pad.
3. Diff the images. What moved is the audio payload; what stayed is the
   directory structure.
4. Vary one variable at a time - quality mode, mono/stereo, start/end points -
   and diff again to locate each field in the header.

The community at sp-forums has done parts of this already; check there before
starting from scratch.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


@dataclass
class CardSample:
    """One sample slot as stored on the card."""
    bank: int
    pad: int
    offset: int
    length_bytes: int
    quality_mode: str      # Standard | Long | LoFi
    stereo: bool
    start_frame: int
    end_frame: int


class SmartMediaReader:
    """Parses a raw card image.

    Only 8-64 MB cards at 3.3 V were supported by the hardware, so any image
    outside that range is either not from an SP-303 or is corrupt.
    """

    MIN_SIZE = 8 * 1024 * 1024
    MAX_SIZE = 64 * 1024 * 1024

    def __init__(self, image_path: Path):
        self.image_path = image_path
        self.size = image_path.stat().st_size

        if not (self.MIN_SIZE <= self.size <= self.MAX_SIZE):
            raise ValueError(
                f"{image_path.name} is {self.size / 1024 / 1024:.1f} MB. "
                f"SP-303 cards are 8-64 MB; this is probably not one."
            )

    def list_samples(self) -> list[CardSample]:
        raise NotImplementedError(
            "The card layout has not been reverse engineered yet. "
            "See the module docstring for the diff-based method."
        )

    def extract(self, sample: CardSample, out_path: Path) -> None:
        raise NotImplementedError("Depends on list_samples().")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path, help="raw card image (.img/.bin)")
    parser.add_argument("--out", type=Path, default=Path("extracted"))
    args = parser.parse_args()

    reader = SmartMediaReader(args.image)
    print(f"Card image: {args.image.name}  ({reader.size / 1024 / 1024:.1f} MB)")

    try:
        samples = reader.list_samples()
    except NotImplementedError as exc:
        print(f"\n  NOT IMPLEMENTED: {exc}\n")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    for sample in samples:
        name = f"bank{sample.bank}_pad{sample.pad}.wav"
        reader.extract(sample, args.out / name)
        print(f"  extracted {name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
