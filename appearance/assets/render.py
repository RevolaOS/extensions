#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the Settings pane icons: a Cupertino Icons glyph each, fitted into
the art box of the stock Settings icons on a 78 x 121 gray coverage mask, lit
from the top and mirrored in a fading reflection below. Drawing uses
ImageMagick's `magick`; the font (CupertinoIcons.ttf from the Flutter
`cupertino_icons` package, MIT, see LICENSE.cupertino-icons) is not in the
repository.

    python3 assets/render.py --font CupertinoIcons.ttf
    python3 assets/render.py --font CupertinoIcons.ttf --check
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
WIDTH, HEIGHT = 78, 121
# Icon file stem, the glyph's code point in the font, and its reach: the
# longer side of the box the glyph is fitted into, centred on the art box.
ICONS = {
    "settings-appearance": (0xF7E8, 52),  # sparkles
    "settings-theme": (0xF72F, 52),  # paintbrush_fill
    "settings-accent": (0xF665, 52),  # eyedropper_full
    "settings-moon": (0xF719, 52),  # moon_fill
    "settings-covers": (0xF809, 52),  # square_lefthalf_fill
}
# The art box of the stock icons, and the light across it.
ART_TOP, ART_BOTTOM, ART_CENTRE = 12, 62, (39, 37)
LIGHT_TOP, LIGHT_BOTTOM = 228, 155
# The reflection mirrors the art about the line below the art and fades out.
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


def render(out, work, font, code, reach):
    glyph = work / f"{code:x}.png"
    magick(
        "-background",
        "black",
        "-fill",
        "white",
        "-font",
        font,
        "-pointsize",
        "400",
        f"label:{chr(code)}",
        "-trim",
        "+repage",
        "-resize",
        f"{reach}x{reach}",
        glyph,
    )
    slope = (LIGHT_TOP - LIGHT_BOTTOM) / (ART_BOTTOM - ART_TOP)
    top = round(LIGHT_TOP + slope * ART_TOP)
    bottom = round(top - slope * (HEIGHT - 1))
    art = work / "art.png"
    magick(
        "-size",
        f"{WIDTH}x{HEIGHT}",
        "xc:black",
        "(",
        glyph,
        "-background",
        "black",
        "-gravity",
        "Center",
        "-extent",
        f"{reach}x{reach}",
        ")",
        "-gravity",
        "NorthWest",
        "-geometry",
        f"+{ART_CENTRE[0] - reach // 2}+{ART_CENTRE[1] - reach // 2}",
        "-compose",
        "Over",
        "-composite",
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
    shift = round(2 * MIRROR - (HEIGHT - 1))
    start = round(MIRROR + 0.5 + (MIRROR - ART_BOTTOM))
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
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "--font", default=os.environ.get("CUPERTINO_ICONS"), help="CupertinoIcons.ttf"
    )
    parser.add_argument("--check", action="store_true", help="fail when a file differs")
    args = parser.parse_args()
    if not args.font:
        sys.exit("give the font with --font or CUPERTINO_ICONS")
    with tempfile.TemporaryDirectory() as name:
        work = Path(name)
        for stem, (code, reach) in ICONS.items():
            final = HERE / f"{stem}.png"
            out = work / final.name if args.check else final
            render(out, work, args.font, code, reach)
            if args.check and out.read_bytes() != final.read_bytes():
                sys.exit(f"{final.name} is out of date: run {Path(__file__).name}")


if __name__ == "__main__":
    main()
