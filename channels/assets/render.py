#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-channels.png, the Settings pane icon: the editor's half
disc with a lobe inside it on a 78 x 121 gray coverage mask in the style of the
stock Settings icons, lit from the top and mirrored in a fading reflection
below. Drawing uses ImageMagick's `magick`.

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
OUT = HERE / "settings-channels.png"
WIDTH, HEIGHT = 78, 121
# The half disc: a ring about CENTRE, the upper half only.
CENTRE = (39, 50)
OUTER, INNER = 26, 22
# The lobe: a quarter-circle fan from the centre, LOBE from it, between the
# arms at 45 degrees.
LOBE = 18
# The light: gray level at the top of the ring falling to the baseline.
LIGHT_TOP, LIGHT_BOTTOM = 228, 155
# The reflection mirrors the art about the line below the baseline and fades out.
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


def fan():
    """Points along the lobe's arc between the arms, left to right."""
    cx, cy = CENTRE
    points = []
    for step in range(0, 91, 3):
        t = math.radians(225 + step)
        points.append(f"{cx + LOBE * math.cos(t):.2f},{cy + LOBE * math.sin(t):.2f}")
    return points


def shapes():
    """magick -draw arguments for the half disc and lobe, white on black."""
    cx, cy = CENTRE
    return [
        "-fill",
        "white",
        "-draw",
        f"ellipse {cx},{cy} {OUTER},{OUTER} 180,360",
        "-fill",
        "black",
        "-draw",
        f"ellipse {cx},{cy} {INNER},{INNER} 180,360",
        "-draw",
        f"rectangle 0,{cy} {WIDTH},{HEIGHT}",
        "-fill",
        "white",
        "-draw",
        "polygon " + " ".join([f"{cx},{cy}", *fan()]),
    ]


def render(out, work):
    # The glyph under a light that fades down the frame.
    art_top, art_bottom = CENTRE[1] - OUTER, CENTRE[1]
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
    with tempfile.TemporaryDirectory(prefix="settings-channels-") as name:
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
