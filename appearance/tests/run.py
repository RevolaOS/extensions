#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the appearance contracts with a staged SDK: python3 tests/run.py SDK.

The extension's logic is built natively, both modules in one program, over a
bitmap bank and a firmware file in host memory. Hook handlers carry pointers in
uint32_t by the ARM convention, so the host build allows the narrowing casts and
reads Apple's struct fields unaligned."""

import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="appearance-tests-") as name:
    work = Path(name)
    for header in ("rv_api.h", "rv_libc.h", "rv_ui.h", "rv_sites.h", "osos-2.0.5.h"):
        (work / header).write_text((sdk / header).read_text())
    executable = work / "contracts"
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-int-to-pointer-cast",
            "-Wno-pointer-to-int-cast",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-sanitize=alignment",
            "-I",
            str(root),
            "-I",
            str(work),
            "-I",
            str(root.parent),
            str(root / "contracts.c"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
print("Appearance contracts passed")
