#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-compressor.png, the Settings pane icon: a waveform pressed
flat against two bars, on a 78 x 121 gray coverage mask in the style of the
stock Settings icons, lit from the top and mirrored in a fading reflection
below. Drawing uses ImageMagick's `magick`.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "settings-compressor.png"
WIDTH, HEIGHT = 78, 121
# The two bars the waveform presses against, and the gap between them.
BAR_LEFT, BAR_RIGHT = 13, 65
BAR_TOP, BAR_BOTTOM, BAR_HEIGHT, BAR_RADIUS = 12, 61, 3, 1.5
GAP_TOP, GAP_BOTTOM = BAR_TOP + BAR_HEIGHT + 3, BAR_BOTTOM - 3
# The waveform: bars of these full heights, centred in the gap and cut to it.
WAVE_HEIGHTS = (12, 24, 40, 52, 40, 24, 12)
WAVE_PITCH, WAVE_WIDTH, WAVE_RADIUS = 7.5, 4.5, 1.5
# The light: gray level at the top bar falling to the bottom bar.
LIGHT_TOP, LIGHT_BOTTOM = 228, 155
# The reflection mirrors the art about the line below the bars and fades out.
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
    """magick -draw arguments for the glyph, white on black."""
    draw = ["-fill", "white"]
    for top in (BAR_TOP, BAR_BOTTOM):
        draw += [
            "-draw",
            (
                f"roundrectangle {BAR_LEFT},{top} {BAR_RIGHT},{top + BAR_HEIGHT} "
                f"{BAR_RADIUS},{BAR_RADIUS}"
            ),
        ]
    centre = (GAP_TOP + GAP_BOTTOM) / 2
    first = (BAR_LEFT + BAR_RIGHT) / 2 - WAVE_PITCH * (len(WAVE_HEIGHTS) - 1) / 2
    for i, height in enumerate(WAVE_HEIGHTS):
        half = min(height, GAP_BOTTOM - GAP_TOP) / 2
        left = first + i * WAVE_PITCH - WAVE_WIDTH / 2
        draw += [
            "-draw",
            (
                f"roundrectangle {left:.2f},{centre - half:.2f} "
                f"{left + WAVE_WIDTH:.2f},{centre + half:.2f} "
                f"{WAVE_RADIUS},{WAVE_RADIUS}"
            ),
        ]
    return draw


def render(out, work):
    # The glyph under a light that fades down the frame.
    art_top, art_bottom = BAR_TOP, BAR_BOTTOM + BAR_HEIGHT
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
    with tempfile.TemporaryDirectory(prefix="settings-compressor-") as name:
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
