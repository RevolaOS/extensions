#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host contracts with a staged SDK: python3 tests/run.py SDK.

The box filter and the geometry have no Apple dependency, so they build
natively under the sanitizers from the sources themselves."""

import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="cover-grid-tests-") as name:
    executable = Path(name) / "contracts"
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=undefined",
            str(root / "contracts.c"),
            str(root.parent / "scale.c"),
            "-lm",
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
