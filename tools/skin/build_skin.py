#!/usr/bin/env python3
"""Generate a panel skin: artwork + hitbox map, from one layout definition.

WHY THIS EXISTS
---------------
A GUI built from a panel image needs two things that must agree exactly: the
picture, and the map of where every control sits on it. Authoring those
separately guarantees they drift - you nudge a button in the artwork, forget
the hitbox, and the pad stops responding where it looks like it should.

So both come out of LAYOUT below. Move a control there and the SVG and the JSON
move together, by construction.

SWAPPING IN A PHOTOGRAPH
------------------------
The plugin reads `panel.json` and whatever `image` points at - .svg or .png/.jpg.
To use a photo of real hardware:

  1. Shoot the unit square-on, evenly lit, no perspective. Correct any keystone.
  2. Drop it in as panel.png and set "image": "panel.png" in panel.json.
  3. Re-measure the hitboxes against the photo's pixel dimensions. Either edit
     LAYOUT here and set SOURCE_W/SOURCE_H to the photo's size, or run
     `--calibrate` to click the controls out interactively.

Coordinates in LAYOUT are pixels of the source image. The loader normalises them
by the image size, so the GUI stays resolution-independent and resizable.

LEGAL
-----
A product photograph is copyrighted by the photographer, and a hardware panel's
appearance can be protected trade dress. Shooting your own unit resolves the
first; the second is a distribution question, not a development one. The default
skin generated here is an original design with the same functional topology.
See docs/LICENSING.md.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

# Source-image coordinate space. The loader normalises against this.
SOURCE_W = 1240
SOURCE_H = 720

# --- palette (matches the project's report styling) ------------------------
BG        = "#12100c"
CHASSIS   = "#211c15"
PANEL     = "#2a231a"
EDGE      = "#4a3f30"
AMBER     = "#e0a458"
AMBER_DIM = "#8a6533"
CREAM     = "#f2e9da"
MUTED     = "#a89a84"
GREEN     = "#7a9e8e"
PAD_FACE  = "#3a3128"
DISPLAY_BG = "#0d0b08"


def grid(x0: int, y0: int, w: int, h: int, gap: int, count: int, cols: int | None = None):
    """Yield (x, y, w, h) laid out left-to-right, wrapping every `cols`."""
    cols = cols or count
    for i in range(count):
        col, row = i % cols, i // cols
        yield (x0 + col * (w + gap), y0 + row * (h + gap), w, h)


# ---------------------------------------------------------------------------
# THE LAYOUT - single source of truth for artwork and hitboxes.
#
# Functional topology mirrors the hardware: 8 pads, 3 CTRL knobs whose meaning
# follows the active effect, dedicated effect buttons, a 3-digit display. The
# visual identity is original.
# ---------------------------------------------------------------------------
def build_layout() -> list[dict]:
    controls: list[dict] = []

    # --- display + context bar ---------------------------------------------
    controls.append({
        "id": "display", "type": "display", "rect": [60, 60, 220, 92],
        "label": "3-digit LED",
    })
    controls.append({
        "id": "context", "type": "label", "rect": [60, 168, 460, 30],
        "label": "context bar",
    })

    # --- master volume ------------------------------------------------------
    controls.append({
        "id": "volume", "type": "knob", "param": "volume",
        "rect": [360, 62, 88, 88], "label": "VOLUME",
    })

    # --- CTRL 1/2/3 ---------------------------------------------------------
    # The expressive core. Their meaning changes with the active effect, which
    # is exactly why the context bar above exists.
    for i, x in enumerate((76, 216, 356)):
        controls.append({
            "id": f"ctrl{i + 1}", "type": "knob", "param": f"ctrl{i + 1}",
            "rect": [x, 246, 108, 108], "label": f"CTRL {i + 1}",
        })

    # --- left button grid: 4 columns x 3 rows -------------------------------
    left_rows = [
        ("quality",  ["STANDARD", "LONG", "LO-FI", "STEREO"],
         ["setQuality:0", "setQuality:1", "setQuality:2", "toggleStereo"]),
        ("playback", ["GATE", "LOOP", "REVERSE", "HOLD"],
         ["toggleGate", "toggleLoop", "toggleReverse", "toggleHold"]),
        ("edit",     ["MARK", "ST/END", "TIME/BPM", "REMAIN"],
         ["mark", "editStartEnd", "editTimeBpm", "remain"]),
    ]
    for row_index, (group, labels, actions) in enumerate(left_rows):
        y = 396 + row_index * 58
        for (x, yy, w, h), label, action in zip(
                grid(60, y, 90, 44, 8, 4), labels, actions):
            controls.append({
                "id": f"{group}_{label.lower().replace('/', '_')}",
                "type": "toggle" if group == "playback" else "button",
                "rect": [x, yy, w, h], "label": label, "action": action,
            })

    # --- effect buttons -----------------------------------------------------
    effects = [
        ("FILTER+DRIVE", 1), ("PITCH", 2), ("DELAY", 3),
        ("VINYL SIM", 4), ("ISOLATOR", 5), ("MFX", 6),
    ]
    for (x, y, w, h), (label, value) in zip(grid(560, 60, 100, 46, 8, 6), effects):
        controls.append({
            "id": f"fx_{label.split('+')[0].split()[0].lower()}",
            "type": "button", "rect": [x, y, w, h], "label": label,
            "action": "selectEffect", "value": value,
        })

    # --- the 8 pads ---------------------------------------------------------
    for i, (x, y, w, h) in enumerate(grid(560, 236, 140, 140, 16, 8, cols=4)):
        controls.append({
            "id": f"pad{i + 1}", "type": "pad", "index": i,
            "rect": [x, y, w, h], "label": str(i + 1),
        })

    # --- banks --------------------------------------------------------------
    for i, (x, y, w, h) in enumerate(grid(560, 556, 68, 46, 8, 4)):
        controls.append({
            "id": f"bank_{'abcd'[i]}", "type": "button", "rect": [x, y, w, h],
            "label": "ABCD"[i], "action": "selectBank", "value": i,
        })

    # --- transport / capture ------------------------------------------------
    actions = [("REC", "rec"), ("RESAMPLE", "resample"),
               ("CANCEL", "cancel"), ("DEL", "delete")]
    for (x, y, w, h), (label, action) in zip(grid(880, 556, 76, 46, 8, 4), actions):
        controls.append({
            "id": f"act_{action}", "type": "button", "rect": [x, y, w, h],
            "label": label, "action": action,
        })

    return controls


# ---------------------------------------------------------------------------
# SVG emitter
# ---------------------------------------------------------------------------
def esc(text: str) -> str:
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def render_svg(controls: list[dict]) -> str:
    font = "Helvetica, Arial, sans-serif"
    out: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{SOURCE_W}" '
        f'height="{SOURCE_H}" viewBox="0 0 {SOURCE_W} {SOURCE_H}">',
        "<defs>",
        '  <linearGradient id="chassis" x1="0" y1="0" x2="0" y2="1">',
        f'    <stop offset="0" stop-color="#2b251c"/>',
        f'    <stop offset="1" stop-color="#1a1610"/>',
        "  </linearGradient>",
        '  <linearGradient id="padFace" x1="0" y1="0" x2="0" y2="1">',
        f'    <stop offset="0" stop-color="#443a2e"/>',
        f'    <stop offset="1" stop-color="#2e271f"/>',
        "  </linearGradient>",
        '  <radialGradient id="knobFace" cx="0.35" cy="0.3" r="0.8">',
        f'    <stop offset="0" stop-color="#4c4234"/>',
        f'    <stop offset="1" stop-color="#241e18"/>',
        "  </radialGradient>",
        "</defs>",
        f'<rect width="{SOURCE_W}" height="{SOURCE_H}" fill="{BG}"/>',
        f'<rect x="14" y="14" width="{SOURCE_W - 28}" height="{SOURCE_H - 28}" '
        f'rx="18" fill="url(#chassis)" stroke="{EDGE}" stroke-width="2"/>',
        f'<rect x="14" y="14" width="{SOURCE_W - 28}" height="6" rx="3" fill="{AMBER_DIM}"/>',
    ]

    # Section divider between the control side and the pad side.
    out.append(f'<line x1="536" y1="48" x2="536" y2="{SOURCE_H - 48}" '
               f'stroke="{EDGE}" stroke-width="2"/>')

    for c in controls:
        x, y, w, h = c["rect"]
        kind = c["type"]
        label = c.get("label", "")
        cx, cy = x + w / 2, y + h / 2

        if kind == "display":
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" '
                       f'fill="{DISPLAY_BG}" stroke="{EDGE}" stroke-width="2"/>')
            out.append(f'<text x="{cx}" y="{y + h * 0.68}" font-family="{font}" '
                       f'font-size="52" font-weight="bold" fill="{AMBER}" '
                       f'text-anchor="middle" letter-spacing="6">000</text>')

        elif kind == "label":
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="4" '
                       f'fill="{PANEL}" stroke="{EDGE}"/>')
            out.append(f'<text x="{x + 10}" y="{y + h * 0.68}" font-family="{font}" '
                       f'font-size="14" fill="{MUTED}">CUTOFF / RESONANCE / DRIVE</text>')

        elif kind == "knob":
            r = min(w, h) / 2
            out.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#knobFace)" '
                       f'stroke="{EDGE}" stroke-width="2"/>')
            out.append(f'<circle cx="{cx}" cy="{cy}" r="{r * 0.62}" fill="none" '
                       f'stroke="{AMBER_DIM}" stroke-width="1.5" opacity="0.6"/>')
            # Pointer at 12 o'clock; the plugin rotates its own indicator on top.
            out.append(f'<line x1="{cx}" y1="{cy - r * 0.30}" x2="{cx}" '
                       f'y2="{cy - r * 0.82}" stroke="{AMBER}" stroke-width="4" '
                       f'stroke-linecap="round"/>')
            out.append(f'<text x="{cx}" y="{y + h + 22}" font-family="{font}" '
                       f'font-size="14" font-weight="bold" fill="{MUTED}" '
                       f'text-anchor="middle">{esc(label)}</text>')

        elif kind == "pad":
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" '
                       f'fill="url(#padFace)" stroke="{EDGE}" stroke-width="2"/>')
            out.append(f'<rect x="{x + 8}" y="{y + 8}" width="{w - 16}" '
                       f'height="{h - 16}" rx="6" fill="none" stroke="{AMBER_DIM}" '
                       f'stroke-width="1" opacity="0.35"/>')
            out.append(f'<text x="{x + 14}" y="{y + 30}" font-family="{font}" '
                       f'font-size="18" font-weight="bold" fill="{AMBER_DIM}">'
                       f'{esc(label)}</text>')

        else:  # button / toggle
            fill = PANEL if kind == "button" else "#2f2a1e"
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" '
                       f'fill="{fill}" stroke="{EDGE}" stroke-width="1.5"/>')
            size = 12 if len(label) > 8 else 13
            out.append(f'<text x="{cx}" y="{cy + 4}" font-family="{font}" '
                       f'font-size="{size}" font-weight="bold" fill="{CREAM}" '
                       f'text-anchor="middle">{esc(label)}</text>')

    # Section captions.
    for text, tx, ty in (("EFFECTS", 560, 44), ("PADS", 560, 224),
                         ("BANK", 560, 546), ("CAPTURE", 880, 546)):
        out.append(f'<text x="{tx}" y="{ty}" font-family="{font}" font-size="12" '
                   f'font-weight="bold" fill="{GREEN}" letter-spacing="2">{text}</text>')

    out.append(f'<text x="{SOURCE_W - 40}" y="{SOURCE_H - 30}" font-family="{font}" '
               f'font-size="13" fill="{AMBER_DIM}" text-anchor="end" '
               f'letter-spacing="3">SAMPLER</text>')
    out.append("</svg>")
    return "\n".join(out)


def render_manifest(controls: list[dict]) -> dict:
    return {
        "name": "default",
        "image": "panel.svg",
        "sourceWidth": SOURCE_W,
        "sourceHeight": SOURCE_H,
        "controls": controls,
        "_comment": (
            "Rects are pixels in the source image; the plugin normalises them "
            "by sourceWidth/sourceHeight. Generated by tools/skin/build_skin.py "
            "- edit LAYOUT there, not this file."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path,
                        default=Path("resources/skins/default"))
    parser.add_argument("--png", action="store_true",
                        help="also rasterise to PNG (needs cairosvg)")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    controls = build_layout()

    svg_path = args.out / "panel.svg"
    svg_path.write_text(render_svg(controls), encoding="utf-8")

    json_path = args.out / "panel.json"
    json_path.write_text(json.dumps(render_manifest(controls), indent=2),
                         encoding="utf-8")

    counts: dict[str, int] = {}
    for c in controls:
        counts[c["type"]] = counts.get(c["type"], 0) + 1

    print(f"\n  {svg_path}   ({SOURCE_W}x{SOURCE_H})")
    print(f"  {json_path}  {len(controls)} controls")
    for kind, n in sorted(counts.items()):
        print(f"      {kind:<9} {n}")

    if args.png:
        try:
            import cairosvg
            cairosvg.svg2png(url=str(svg_path),
                             write_to=str(args.out / "panel.png"),
                             output_width=SOURCE_W * 2)
            print(f"  {args.out / 'panel.png'}  (2x)")
        except ImportError:
            print("\n  --png needs cairosvg: pip install cairosvg")
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
