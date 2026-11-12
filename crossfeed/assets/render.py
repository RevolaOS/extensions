#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-crossfeed.png, the Settings pane icon: a pair of headphones
on a 78 x 121 gray coverage mask in the style of the stock Settings icons, lit
from the top and mirrored in a fading reflection below. Drawing uses
ImageMagick's `magick`.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import argparse
import math
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "settings-crossfeed.png"
WIDTH, HEIGHT = 78, 121
# The headband: the upper half of a superellipse (exponent BAND_SHAPE, between an
# ellipse and a rounded box) centred on BAND_CENTRE, outer and inner half-axes
# as given, with straight legs from the centre line down to LEG_BOTTOM.
BAND_CENTRE = (39, 32)
BAND_OUTER, BAND_INNER = (26.5, 20), (21.5, 15.5)
BAND_SHAPE = 2.3
LEG_BOTTOM = 51
# The cups: 11 x 21 with rounded corners, hanging inside the legs.
CUPS = (22, 46)
CUP_TOP, CUP_WIDTH, CUP_HEIGHT, CUP_RADIUS = 41, 10.5, 20.5, 3
# The light: gray level at the top of the band falling to the bottom of the cups.
LIGHT_TOP, LIGHT_BOTTOM = 228, 155
# The reflection mirrors the art about the line below the cups and fades out.
MIRROR = 77.5
REFLECTION = (0.40, 0.02)
GRAY8 = (
    "-colorspace",
    "gray",
    "-depth",
    "8",
    "-define",
    "png:color-type=0",
    "-define",
    "png:bit-depth=8",
)


def magick(*args):
    subprocess.run(["magick", *map(str, args)], check=True)


def arch(a, b):
    """Points along the upper half of the superellipse, left to right."""
    cx, cy = BAND_CENTRE
    points = []
    for step in range(0, 181, 2):
        t = math.radians(step)
        c, s = math.cos(t), math.sin(t)
        x = cx - math.copysign(abs(c) ** (2 / BAND_SHAPE), c) * a
        y = cy - abs(s) ** (2 / BAND_SHAPE) * b
        points.append(f"{x:.2f},{y:.2f}")
    return points


def shapes():
    """magick -draw arguments for the headphones, white on black."""
    cx = BAND_CENTRE[0]
    (oa, ob), (ia, ib) = BAND_OUTER, BAND_INNER
    outer = [f"{cx - oa},{LEG_BOTTOM}", *arch(oa, ob), f"{cx + oa},{LEG_BOTTOM}"]
    inner = [
        f"{cx + ia},{LEG_BOTTOM + 1}",
        *reversed(arch(ia, ib)),
        f"{cx - ia},{LEG_BOTTOM + 1}",
    ]
    band = "M " + " L ".join(outer + inner) + " Z"
    draw = [
        "-fill",
        "white",
        "-draw",
        f"path '{band}'",
        "-fill",
        "black",
        "-draw",
        f"rectangle 0,{LEG_BOTTOM} {WIDTH},{HEIGHT}",
        "-fill",
        "white",
    ]
    for left in CUPS:
        draw += [
            "-draw",
            f"roundrectangle {left},{CUP_TOP} "
            f"{left + CUP_WIDTH},{CUP_TOP + CUP_HEIGHT} {CUP_RADIUS},{CUP_RADIUS}",
        ]
    return draw


def render(out, work):
    # The headphones under a light that fades down the frame.
    art_top, art_bottom = BAND_CENTRE[1] - BAND_OUTER[1], CUP_TOP + CUP_HEIGHT
    slope = (LIGHT_TOP - LIGHT_BOTTOM) / (art_bottom - art_top)
    top = round(LIGHT_TOP + slope * art_top)
    bottom = round(top - slope * (HEIGHT - 1))
    art = work / "art.png"
    magick(
        "-size",
        f"{WIDTH}x{HEIGHT}",
        "xc:black",
        *shapes(),
        "(",
        "-size",
        f"{WIDTH}x{HEIGHT}",
        f"gradient:gray({top})-gray({bottom})",
        ")",
        "-compose",
        "Multiply",
        "-composite",
        art,
    )
    # Its reflection: flipped, moved below the mirror line, faded.
    shift = round(2 * MIRROR - (HEIGHT - 1))
    start = round(MIRROR + 0.5 + (MIRROR - art_bottom))
    fade = work / "fade.png"
    magick(
        "-size",
        f"{WIDTH}x{HEIGHT}",
        "xc:black",
        "(",
        "-size",
        f"{WIDTH}x{HEIGHT - start}",
        f"gradient:gray({REFLECTION[0] * 100}%)-gray({REFLECTION[1] * 100}%)",
        ")",
        "-geometry",
        f"+0+{start}",
        "-composite",
        fade,
    )
    magick(
        art,
        "(",
        art,
        "-flip",
        "-roll",
        f"+0+{shift}",
        fade,
        "-compose",
        "Multiply",
        "-composite",
        ")",
        "-compose",
        "Lighten",
        "-composite",
        *GRAY8,
        out,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="render into a temporary folder and compare with the committed file",
    )
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="settings-crossfeed-") as name:
        work = Path(name)
        if not args.check:
            render(OUT, work)
            return
        fresh = work / OUT.name
        render(fresh, work)
        differing = subprocess.run(
            ["magick", "compare", "-metric", "AE", str(OUT), str(fresh), "null:"],
            capture_output=True,
            text=True,
            check=False,
        ).stderr.split()[0]
        if differing != "0":
            sys.exit(
                f"render.py: {OUT.name} differs from a fresh render: {differing} px"
            )
        print(f"{OUT.name} matches")


if __name__ == "__main__":
    main()
