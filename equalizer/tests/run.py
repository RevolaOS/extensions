#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import cmath
import ctypes
import math
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
SCALE = 1 << 24
RATES = (44100, 48000)
Biquad = ctypes.c_int32 * 5


def build(work, name, sources, *flags):
    library = work / name
    subprocess.run(
        ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-shared", "-fPIC"]
        + [
            "-fsanitize=undefined",
            "-fno-sanitize-recover=undefined",
            f"-I{sdk}",
            f"-I{sdk}/extension",
            *flags,
        ]
        + [str(source) for source in sources]
        + ["-o", str(library)],
        check=True,
    )
    return ctypes.CDLL(str(library))


def run_engine_test(work):
    """The engine against a double-precision cascade, in tests/engine_test.c."""
    program = work / "engine_test"
    subprocess.run(
        ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g"]
        + [
            "-fsanitize=undefined",
            "-fno-sanitize-recover=undefined",
            f"-I{sdk}",
            f"-I{sdk}/extension",
        ]
        + ["-D_FORTIFY_SOURCE=0"]  # macOS string.h would wrap what rv_libc.h declares
        + [str(root / "engine_test.c"), str(root.parent / "cascade.c")]
        + [
            str(root.parent / "biquad.c"),
            str(sdk / "extension/rv_math.c"),
            "-lm",
            "-o",
            str(program),
        ],
        check=True,
    )
    subprocess.run([str(program)], check=True)


def close(actual, expected, delta):
    assert abs(actual - expected) <= delta, (actual, expected, delta)


def power(bank, hz, rate):
    z = cmath.exp(-2j * math.pi * hz / rate)
    result = 1.0
    for b0, b1, b2, a1, a2 in bank:
        result *= abs((b0 + b1 * z + b2 * z * z) / (SCALE + a1 * z + a2 * z * z)) ** 2
    return result


def reference(kind, hz, gain, precut, rate):
    """The RBJ cookbook biquad at Q 0.7 as b0, b1, b2, a1, a2."""
    if gain == 0:
        return [10 ** (-precut / 200), 0, 0, 0, 0]
    w = 2 * math.pi * hz / rate
    c, alpha = math.cos(w), math.sin(w) / 1.4
    a = 10 ** (gain / 400)
    beta = 2 * math.sqrt(a) * alpha
    if kind == 1:
        b = [1 + alpha * a, -2 * c, 1 - alpha * a]
        d = [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif kind == 2:
        b = [
            a * (a + 1 - (a - 1) * c + beta),
            2 * a * (a - 1 - (a + 1) * c),
            a * (a + 1 - (a - 1) * c - beta),
        ]
        d = [
            a + 1 + (a - 1) * c + beta,
            -2 * (a - 1 + (a + 1) * c),
            a + 1 + (a - 1) * c - beta,
        ]
    else:
        b = [
            a * (a + 1 + (a - 1) * c + beta),
            -2 * a * (a - 1 + (a + 1) * c),
            a * (a + 1 + (a - 1) * c - beta),
        ]
        d = [
            a + 1 - (a - 1) * c + beta,
            2 * (a - 1 - (a + 1) * c),
            a + 1 - (a - 1) * c - beta,
        ]
    return [n * 10 ** (-precut / 200) / d[0] for n in b] + [n / d[0] for n in d[1:]]


def biquad_tests(dll):
    biquad = dll.eq_biquad
    biquad.argtypes = [ctypes.POINTER(ctypes.c_int32)] + [ctypes.c_uint] * 3
    biquad.argtypes += [ctypes.c_int, ctypes.c_uint, ctypes.c_uint]
    response = dll.eq_response
    response.argtypes = [ctypes.POINTER(Biquad), ctypes.c_uint]
    response.argtypes += [ctypes.POINTER(ctypes.c_int16), ctypes.c_uint]
    response.restype = None
    peak = dll.eq_peak
    peak.argtypes = [ctypes.POINTER(Biquad), ctypes.c_uint, ctypes.c_uint]
    attenuate = dll.eq_attenuate
    attenuate.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_uint]
    attenuate.restype = None

    for rate in RATES:
        # A flat band is the precut alone, in steps of a tenth of a decibel.
        previous = SCALE + 1
        for precut in range(101):
            out = Biquad()
            assert biquad(out, 0, 60, 7, 0, precut, rate) == 1
            assert out[0] < previous
            close(out[0], SCALE * 10 ** (-precut / 200), 0.51)
            assert list(out)[1:] == [0, 0, 0, 0]
            previous = out[0]
        for kind in (1, 2, 3):
            for hz in (20, 1000, 20000):
                for gain in (-120, 0, 120):
                    for precut in (0, 1, 37, 100):
                        out = Biquad()
                        assert biquad(out, kind, hz, 7, gain, precut, rate) == 1
                        expected = reference(kind, hz, gain, precut, rate)
                        for a, b in zip(out, expected, strict=True):
                            close(a / SCALE, b, 1 / SCALE)
    out = Biquad()
    for bad in (
        (4, 60, 7, 0, 0, 44100),
        (0, 19, 7, 0, 0, 44100),
        (0, 20001, 7, 0, 0, 44100),
        (0, 60, 2, 0, 0, 44100),
        (0, 60, 41, 0, 0, 44100),
        (0, 60, 7, -121, 0, 44100),
        (0, 60, 7, 121, 0, 44100),
        (0, 60, 7, 0, 101, 44100),
        (0, 60, 7, 0, 0, 32000),
    ):
        assert biquad(out, *bad) == 0, bad

    bands = ((2, 80), (1, 1000), (3, 16000), (1, 250), (1, 4000))
    for rate in RATES:
        for q in (3, 7, 40):
            for gain in (-120, 0, 120):
                bank = (Biquad * 5)()
                for band, (kind, hz) in enumerate(bands):
                    precut = 37 if band == 0 else 0
                    assert biquad(bank[band], kind, hz, q, gain, precut, rate) == 1
                for points in (7, 81, 261):
                    curve = (ctypes.c_int16 * points)()
                    response(bank, rate, curve, points)
                    for i in range(points):
                        hz = 20 * 1000 ** (i / (points - 1))
                        expected = 100 * math.log10(power(bank, hz, rate))
                        expected = max(-480, min(480, expected))
                        close(curve[i], expected, 0.52)

    def dense(bank, count, rate):
        stages = [list(bank[i]) for i in range(count)]
        points = (20 * 1000 ** (i / 6000) for i in range(6001))
        return 100 * math.log10(max(power(stages, hz, rate) for hz in points))

    # Narrow bells near Nyquist, overlapping peaks, resonant shelves and cuts.
    cases = [
        [(1, 19000, 40, 120)],
        [(1, 16000, 40, 120), (1, 12500, 30, 60)],
        [(1, 1000, 7, 60), (1, 1250, 40, 40), (2, 80, 7, 30)],
        [(2, 1000, 40, 90), (3, 1000, 26, -60)],
        [(3, 8000, 7, 75), (1, 4000, 12, 50), (2, 60, 7, 75)],
        [(1, 250, 10, -120), (1, 4000, 10, -60)],
    ]
    for rate in RATES:
        for case in cases:
            bank = (Biquad * 5)()
            for i, (kind, hz, q, gain) in enumerate(case):
                assert biquad(bank[i], kind, hz, q, gain, 0, rate) == 1
            expected = dense(bank, len(case), rate)
            actual = peak(bank, len(case), rate)
            assert expected - 0.01 <= actual <= math.ceil(expected) + 1, (rate, case)
            # Precut rounds up, leaving the peak within 0.1 dB below 0 dB.
            if actual > 0:
                attenuate(bank[0], actual)
                assert peak(bank, len(case), rate) <= 0, (rate, case)
                assert dense(bank, len(case), rate) > -1.01, (rate, case)
    assert peak((Biquad * 5)(), 6, 44100) == 0

    coefficients = Biquad(SCALE, -SCALE // 2, 1000, 7, 9)
    attenuate(coefficients, 60)
    gain = 10 ** (-60 / 200)
    expected = [round(SCALE * gain), round(-SCALE // 2 * gain), round(1000 * gain)]
    assert list(coefficients) == expected + [7, 9]


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="equalizer-tests-") as name:
    work = Path(name)
    biquad_tests(
        build(
            work, "biquad.so", [root.parent / "biquad.c", sdk / "extension/rv_math.c"]
        )
    )
    run_engine_test(work)
print("Equalizer tests passed")
