#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run native-contract fixtures with a staged SDK: python3 tests/run.py SDK."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="progress-indicator-tests-") as name:
    work = Path(name)
    # Widen pointer-bearing native layouts on the host. BMap bytes remain ARM-format.
    header = (sdk / "osos-2.0.5.h").read_text()
    prefixes = (
        "MediaItem_",
        "MediaPlayer_",
        "MediaListModel_",
        "SongModel_",
        "Bitmap_",
    )

    def widen(match):
        key, value = match.groups()
        return (
            f"#define {key} {int(value, 16) * 2:#x}"
            if key.startswith(prefixes)
            else match[0]
        )

    header = re.sub(r"#define (\w+) (0x[0-9a-f]+)", widen, header)
    (work / "osos-2.0.5.h").write_text(header)
    api = (sdk / "extension/rv_api.h").read_text()
    api = re.sub(r"uint32_t ([abcdef])([,)])", r"uintptr_t \1\2", api)
    (work / "rv_api.h").write_text(api)
    for file in ("rv_ui.h", "rv_sites.h", "rv_media.h"):
        (work / file).write_text((sdk / ("extension/" + file)).read_text())
    executable = work / "contracts"
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-I",
            str(work),
            str(root / "contracts.c"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
print("Progress Indicator contracts passed")
