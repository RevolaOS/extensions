#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the row, seek and play tests with a staged SDK: python3 tests/run.py SDK."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="chapters-tests-") as name:
    work = Path(name)
    # Widen the pointer-bearing native layouts for the host; record offsets stay.
    header = (sdk / "osos-2.0.5.h").read_text()

    def widen(match):
        key, value = match.groups()
        widened = ("MediaItem_", "NativeString_", "CppString_", "SongModel_")
        if key.startswith(widened + ("MediaListController_",)):
            return f"#define {key} {int(value, 16) * 2:#x}"
        return match[0]

    (work / "osos-2.0.5.h").write_text(
        re.sub(r"#define (\w+) (0x[0-9a-f]+)", widen, header)
    )
    (work / "rv_libc.h").write_text((sdk / "rv_libc.h").read_text())
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
print("Chapters contracts passed")
