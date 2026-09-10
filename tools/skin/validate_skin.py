#!/usr/bin/env python3
"""Validate a panel skin before the plugin ever loads it.

A skin fails in ways that are painful to debug from inside a DAW: a pad whose
hitbox overlaps its neighbour steals clicks, a control nudged off-canvas simply
stops responding, a knob with no `param` silently does nothing. All of those
look like plugin bugs and are actually data bugs.

Checking it here turns a confusing runtime symptom into a build-time error.

Run after regenerating a skin, or after re-measuring hitboxes against a
photograph.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from xml.etree import ElementTree

REQUIRED_TOP_LEVEL = ("name", "image", "sourceWidth", "sourceHeight", "controls")
KNOWN_TYPES = {"pad", "knob", "button", "toggle", "display", "label", "meter"}


def overlaps(a: list[int], b: list[int]) -> bool:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return not (ax + aw <= bx or bx + bw <= ax or ay + ah <= by or by + bh <= ay)


def validate(skin_dir: Path) -> list[str]:
    errors: list[str] = []
    manifest_path = skin_dir / "panel.json"

    if not manifest_path.exists():
        return [f"missing {manifest_path}"]

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"panel.json is not valid JSON: {exc}"]

    for key in REQUIRED_TOP_LEVEL:
        if key not in manifest:
            errors.append(f"manifest missing required key '{key}'")
    if errors:
        return errors

    width = manifest["sourceWidth"]
    height = manifest["sourceHeight"]

    # The artwork must exist, and if it is SVG its declared size must match the
    # coordinate space the hitboxes were authored in - otherwise every hitbox
    # is offset by a constant scale factor and nothing lines up.
    image_path = skin_dir / manifest["image"]
    if not image_path.exists():
        errors.append(f"image '{manifest['image']}' not found in {skin_dir}")
    elif image_path.suffix.lower() == ".svg":
        try:
            root = ElementTree.parse(image_path).getroot()
            svg_w = root.get("width")
            svg_h = root.get("height")
            if svg_w and int(float(svg_w)) != width:
                errors.append(
                    f"SVG width {svg_w} != manifest sourceWidth {width} - "
                    f"hitboxes will be offset")
            if svg_h and int(float(svg_h)) != height:
                errors.append(
                    f"SVG height {svg_h} != manifest sourceHeight {height} - "
                    f"hitboxes will be offset")
        except ElementTree.ParseError as exc:
            errors.append(f"{image_path.name} is not valid XML: {exc}")

    controls = manifest["controls"]
    seen_ids: set[str] = set()
    pad_indices: list[int] = []
    interactive: list[tuple[str, list[int]]] = []

    for control in controls:
        cid = control.get("id", "<no id>")

        if cid in seen_ids:
            errors.append(f"duplicate control id '{cid}'")
        seen_ids.add(cid)

        ctype = control.get("type")
        if ctype not in KNOWN_TYPES:
            errors.append(f"'{cid}': unknown type '{ctype}'")

        rect = control.get("rect")
        if not (isinstance(rect, list) and len(rect) == 4):
            errors.append(f"'{cid}': rect must be [x, y, w, h]")
            continue

        x, y, w, h = rect
        if w <= 0 or h <= 0:
            errors.append(f"'{cid}': non-positive size {w}x{h}")
        if x < 0 or y < 0 or x + w > width or y + h > height:
            errors.append(
                f"'{cid}': rect {rect} falls outside the {width}x{height} "
                f"canvas - it will be invisible and unclickable")

        # A control that cannot be hit is worse than one that is missing: it
        # looks present and does nothing.
        if ctype in ("pad", "knob", "button", "toggle"):
            if w < 20 or h < 20:
                errors.append(f"'{cid}': {w}x{h} is too small to hit reliably")
            interactive.append((cid, rect))

        if ctype == "knob" and not control.get("param"):
            errors.append(f"'{cid}': knob has no 'param' - it would do nothing")

        if ctype in ("button", "toggle") and not control.get("action"):
            errors.append(f"'{cid}': {ctype} has no 'action' - it would do nothing")

        if ctype == "pad":
            index = control.get("index")
            if not isinstance(index, int):
                errors.append(f"'{cid}': pad has no integer 'index'")
            else:
                pad_indices.append(index)

    # Overlapping hitboxes: the first match wins, so the other control is dead.
    for i in range(len(interactive)):
        for j in range(i + 1, len(interactive)):
            id_a, rect_a = interactive[i]
            id_b, rect_b = interactive[j]
            if overlaps(rect_a, rect_b):
                errors.append(
                    f"'{id_a}' and '{id_b}' overlap - one of them will never "
                    f"receive clicks")

    expected_pads = sorted(range(8))
    if sorted(pad_indices) != expected_pads:
        errors.append(
            f"pad indices are {sorted(pad_indices)}, expected {expected_pads} - "
            f"the hardware has exactly 8 pads per bank")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("skin", type=Path, nargs="?",
                        default=Path("resources/skins/default"))
    args = parser.parse_args()

    errors = validate(args.skin)

    if errors:
        print(f"\n  {args.skin}: {len(errors)} problem(s)\n")
        for error in errors:
            print(f"    - {error}")
        print()
        return 1

    manifest = json.loads((args.skin / "panel.json").read_text(encoding="utf-8"))
    print(f"\n  {args.skin}: OK  "
          f"({len(manifest['controls'])} controls, "
          f"{manifest['sourceWidth']}x{manifest['sourceHeight']})\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
