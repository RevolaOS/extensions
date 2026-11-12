#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the mark selection tests: python3 tests/run.py SDK (the SDK is not needed)."""

import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="chapter-marks-tests-") as name:
    executable = Path(name) / "contracts"
    subprocess.run(
        ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
         "-fsanitize=address,undefined", str(root / "contracts.c"), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
