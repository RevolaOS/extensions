#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-eq.png, the Settings pane icon: three mixer faders on a
78 x 121 gray coverage mask in the style of the stock Settings icons, lit from
the top and mirrored in a fading reflection below. Drawing uses ImageMagick's
`magick`.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "settings-eq.png"
WIDTH, HEIGHT = 78, 121
# Fader tracks: 4 px wide, their centres, top and bottom; the half-pixel
# edges come out as the soft outline of the stock art.
TRACKS = (19, 39, 59)
TRACK_TOP, TRACK_BOTTOM = 13.6, 62.6
# Knobs: 16 x 12 with a one-pixel rim, each at its own position.
KNOB_TOPS = (24, 41, 24)
KNOB_WIDTH, KNOB_HEIGHT = 16, 12
RIM = 0.87
# The light: gray level at the top of the tracks falling to the bottom.
LIGHT_TOP, LIGHT_BOTTOM = 228, 153
# The reflection mirrors the art about the line below the tracks and fades out.
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


def shapes():
    """magick -draw arguments for the faders, white on black."""
    draw = ["-fill", "white"]
    for x in TRACKS:
        draw += [
            "-draw",
            f"roundrectangle {x - 2.5},{TRACK_TOP} {x + 2.5},{TRACK_BOTTOM} 2,2",
        ]
    for x, top in zip(TRACKS, KNOB_TOPS):
        left, right = x - KNOB_WIDTH / 2 - 0.5, x + KNOB_WIDTH / 2 + 0.5
        bottom = top + KNOB_HEIGHT
        draw += [
            "-fill",
            f"gray({RIM * 100}%)",
            "-draw",
            f"roundrectangle {left},{top - 0.2} {right},{bottom + 0.3} 1.5,1.5",
            "-fill",
            "white",
            "-draw",
            f"rectangle {left + 1.5},{top + 1} {right - 1.5},{bottom - 1}",
        ]
    return draw


def render(out, work):
    # The faders under a light that fades down the frame.
    slope = (LIGHT_TOP - LIGHT_BOTTOM) / (TRACK_BOTTOM - TRACK_TOP)
    top = round(LIGHT_TOP + slope * TRACK_TOP)
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
    start = round(MIRROR + 0.5 + (MIRROR - TRACK_BOTTOM))
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
    with tempfile.TemporaryDirectory(prefix="settings-eq-") as name:
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
