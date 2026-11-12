#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import ctypes
import math
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
UNITY = 1 << 24
SHIFT = 13
RATES = (44100, 48000, 32000, 22050, 8000)
RATIOS = (2, 4, 6, 10, 0)
OVER_DB = 0xC0A8C / 65536
HEADROOM = 0x4AC4 / 65536


class Set(ctypes.Structure):
    _fields_ = [
        ("threshold", ctypes.c_int8),
        ("makeup", ctypes.c_uint8),
        ("ratio", ctypes.c_uint8),
        ("knee", ctypes.c_uint8),
        ("attack", ctypes.c_uint16),
        ("release", ctypes.c_uint16),
    ]


class Points(ctypes.Structure):
    _fields_ = [
        ("db", ctypes.c_int32 * 5),
        ("offset", ctypes.c_int32 * 5),
        ("makeup", ctypes.c_int32),
    ]


class Design(ctypes.Structure):
    _fields_ = [("gain", ctypes.c_int32 * 66), ("makeup", ctypes.c_int32)] + [
        (name, ctypes.c_int32)
        for name in (
            "attack_a",
            "attack_b",
            "release_a",
            "release_b",
            "limit_a",
            "hp1",
            "hp2",
        )
    ]


def build(work, name, sources):
    library = work / name
    subprocess.run(
        ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-shared", "-fPIC"]
        + ["-fsanitize=undefined", "-fno-sanitize-recover=undefined"]
        + [f"-I{sdk}", f"-I{root.parent}", "-D_FORTIFY_SOURCE=0"]
        + [str(source) for source in sources]
        + ["-o", str(library)],
        check=True,
    )
    return ctypes.CDLL(str(library))


def reference_points(threshold, makeup, ratio, knee):
    """Rockbox's curve in floating point: input levels in dB, the gain offset
    at each, and the makeup gain."""
    r = RATIOS[ratio]

    def slope(db):
        return -db * (r - 1) / r if r else -db

    half = 3 if knee else 0
    at = [threshold - half, threshold, threshold + half, 0, OVER_DB]
    top, full = slope(half), slope(-threshold)
    offset = [0, top / 2, top, full, slope(OVER_DB) + full]
    return at, offset, (-full - HEADROOM) if makeup else 0


def reference_offset(db, points, knee):
    """The gain offset in dB for an input level, as Rockbox fills its table."""
    at, offset, _ = points
    if db <= at[0]:
        return 0
    if knee and db <= at[2]:
        return (db - at[0]) / 6 * offset[2]
    return (at[1] - db) / at[1] * offset[3]


def to_db(gain):
    return 20 * math.log10(gain / UNITY)


def every_set():
    for threshold in range(1, 9):
        for ratio in range(5):
            for knee in range(2):
                for makeup in range(2):
                    yield Set(
                        threshold=-3 * threshold,
                        makeup=makeup,
                        ratio=ratio,
                        knee=knee,
                        attack=5,
                        release=500,
                    )


def coefficient(rate, ms):
    return UNITY // rate * 1152 // ms


def design_tests(dll):
    points = dll.compressor_points
    points.argtypes = [ctypes.POINTER(Set), ctypes.POINTER(Points)]
    points.restype = None
    design = dll.compressor_design
    design.argtypes = [ctypes.POINTER(Design), ctypes.POINTER(Set), ctypes.c_uint]
    design.restype = None

    for chosen in every_set():
        reference = reference_points(
            chosen.threshold, chosen.makeup, chosen.ratio, chosen.knee
        )
        out = Points()
        points(chosen, out)
        for i in range(5):
            assert abs(out.db[i] / 65536 - reference[0][i]) < 2 / 65536, (
                chosen.threshold,
                i,
            )
            assert abs(out.offset[i] / 65536 - reference[1][i]) < 2 / 65536, (
                chosen.ratio,
                i,
            )
        assert abs(out.makeup / 65536 - reference[2]) < 2 / 65536

        d = Design()
        design(d, chosen, 44100)
        assert d.gain[0] == UNITY
        for i in range(1, 64):
            level = 20 * math.log10(i / 64)
            expected = reference_offset(level, reference, chosen.knee)
            assert abs(to_db(d.gain[i]) - expected) < 0.01, (
                chosen.threshold,
                chosen.ratio,
                i,
            )
            assert d.gain[i] <= UNITY
        assert abs(to_db(d.gain[64]) - reference[1][3]) < 0.005
        assert abs(to_db(d.gain[65]) - reference[1][4]) < 0.005
        assert abs(to_db(d.makeup) - reference[2]) < 0.005
        # Monotonic: louder input, no more gain.
        assert all(a >= b for a, b in zip(d.gain, d.gain[1:]))

    # Coefficients are Rockbox's integer formulas, and near the real values.
    for rate in RATES:
        for attack, release in ((0, 100), (5, 500), (30, 1000)):
            chosen = Set(
                threshold=-12, makeup=1, ratio=1, knee=1, attack=attack, release=release
            )
            d = Design()
            design(d, chosen, rate)
            assert d.release_a == coefficient(rate, release)
            assert d.release_b == UNITY - d.release_a
            assert abs(d.release_a / UNITY - 1152 / (rate * release)) < 0.02 * 1152 / (
                rate * release
            )
            if attack:
                assert d.attack_a == coefficient(rate, attack)
                assert d.attack_b == UNITY - d.attack_a
            else:
                assert d.attack_a == UNITY and d.attack_b == 0
            assert d.limit_a == coefficient(rate, 3)
            assert d.hp1 == UNITY // (rate + 0x3C1) * rate
            assert d.hp2 == UNITY // (rate + 0x2065) * rate
            assert abs(d.hp1 / UNITY - rate / (rate + 961)) < 3e-3
            assert abs(d.hp2 / UNITY - rate / (rate + 8293)) < 3e-3
    # Makeup Off is exactly unity.
    d = Design()
    design(
        d, Set(threshold=-12, makeup=0, ratio=4, knee=0, attack=5, release=500), 44100
    )
    assert d.makeup == UNITY


def label_tests(dll):
    label = dll.compressor_label
    label.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
    label.restype = ctypes.c_char_p
    out = ctypes.create_string_buffer(16)

    def labels(parameter, count):
        return [label(parameter, i, out).decode() for i in range(count)]

    THRESHOLD, RATIO, KNEE, MAKEUP, ATTACK, RELEASE = range(6)
    assert labels(THRESHOLD, 9) == ["Off"] + [f"{-3 * i} dB" for i in range(1, 9)]
    assert labels(MAKEUP, 2) == ["Off", "Auto"]
    assert labels(RATIO, 5) == ["2:1", "4:1", "6:1", "10:1", "Limit"]
    assert labels(KNEE, 2) == ["Hard", "Soft"]
    assert labels(ATTACK, 7) == [f"{5 * i} ms" for i in range(7)]
    assert labels(RELEASE, 10) == [f"{100 + 100 * i} ms" for i in range(10)]
    defaults = (0, 1, 1, 1, 1, 4)
    counts = (9, 5, 2, 2, 7, 10)
    chosen = [labels(p, c)[d] for p, (c, d) in enumerate(zip(counts, defaults))]
    assert chosen == ["Off", "4:1", "Soft", "Auto", "5 ms", "500 ms"]


def wrap(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def scale(a, b):
    return wrap((a * b) >> 24)


def fracmul(a, b):
    return wrap((a * b) >> 31)


def magnitude(sample):
    return -(sample + 1) if sample < 0 else sample


def saturate(value):
    return max(-32768, min(32767, value))


class Reference:
    """The ported loop, integer for integer, on S16 raised by SHIFT bits."""

    def __init__(self, rate):
        self.length = 3 * rate // 1000
        self.delay = [(0, 0)] * self.length
        self.index = 0
        self.gain = UNITY
        self.holdoff = self.length
        self.x1 = self.y1 = self.y2 = 0
        self.clamped = 0

    def curve_gain(self, design, level):
        gain = design.gain
        level = magnitude(level) >> SHIFT
        if level < 1 << 15:
            i = level >> 9
            return wrap(gain[i] - fracmul((level & 0x1FF) << 22, gain[i] - gain[i + 1]))
        if level < 1 << 17:
            return wrap(
                gain[64]
                - fracmul(((level - (1 << 15)) // 3) << 16, gain[64] - gain[65])
            )
        return -1

    def process(self, design, frames):
        out = []
        for s0, s1 in frames:
            left, right = s0 << SHIFT, s1 << SHIFT
            x = (left + right) >> 1
            peak = max(magnitude(left), magnitude(right))
            self.y1 = scale(design.hp1, x - self.x1 + self.y1)
            self.y2 = scale(design.hp2, x - self.x1 + self.y2)
            self.x1 = x
            level = wrap((x >> 1) + self.y1 + self.y2 * 2)
            level >>= 1
            level = wrap(level + (level >> 1))
            wanted = self.curve_gain(design, level)
            if wanted <= self.gain and wanted > 0:
                if design.attack_a == UNITY:
                    self.gain = wanted
                else:
                    self.gain = wrap(
                        scale(self.gain, design.attack_b)
                        + scale(wanted, design.attack_a)
                    )
                self.holdoff = self.length
            elif self.holdoff > 0:
                self.holdoff -= 1
            else:
                self.gain = wrap(
                    scale(self.gain, design.release_b) + scale(wanted, design.release_a)
                )
            total = scale(self.gain, design.makeup)
            if wrap((total * peak) >> 28) > UNITY:
                self.gain = wrap(self.gain - design.limit_a)
            d0, d1 = self.delay[self.index]
            self.delay[self.index] = (s0, s1)
            self.index = (self.index + 1) % self.length
            if total != UNITY:
                o0 = wrap((total * d0 + (1 << 23)) >> 24)
                o1 = wrap((total * d1 + (1 << 23)) >> 24)
                self.clamped += (saturate(o0) != o0) + (saturate(o1) != o1)
                out.append((saturate(o0), saturate(o1)))
            else:
                out.append((d0, d1))
        return out


def noise(count, seed, scale_bits=0):
    value = seed
    frames = []
    for _ in range(count):
        pair = []
        for _ in range(2):
            value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
            sample = value >> 16
            sample = sample - 65536 if sample >= 32768 else sample
            pair.append(sample >> scale_bits)
        frames.append(tuple(pair))
    return frames


def sine(count, hz, rate, amplitude):
    return [
        (round(amplitude * math.sin(2 * math.pi * hz * i / rate)),) * 2
        for i in range(count)
    ]


def process_tests(dll):
    publish = dll.compressor_publish
    publish.argtypes = [ctypes.POINTER(Set)]
    publish.restype = None
    process = dll.compressor_process
    process.argtypes = [
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_uint32,
        ctypes.c_uint32,
    ]
    process.restype = None
    design = dll.compressor_design
    design.argtypes = [ctypes.POINTER(Design), ctypes.POINTER(Set), ctypes.c_uint]
    design.restype = None
    count = 512

    def run(frames, rate):
        buffer = (ctypes.c_int16 * (2 * len(frames)))(
            *[s for frame in frames for s in frame]
        )
        process(buffer, len(frames), rate)
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(len(frames))]

    def designed(chosen, rate):
        d = Design()
        design(d, chosen, rate)
        return d

    def interrupts():
        dll.test_irq_calls.restype = ctypes.c_uint32
        return dll.test_irq_calls()

    # Off leaves the stream alone.
    publish(Set(threshold=0))
    frames = noise(count, 1)
    assert run(frames, 44100) == frames
    assert interrupts() == 2

    # On at three rates, bit for bit against the model across blocks; the
    # first look-ahead's worth of frames is the silence the delay line held.
    chosen = Set(threshold=-12, makeup=1, ratio=1, knee=1, attack=5, release=500)
    for rate in (44100, 48000, 8000):
        publish(chosen)
        d = designed(chosen, rate)
        reference = Reference(rate)
        for block in range(3):
            frames = noise(count, block + rate)
            got = run(frames, rate)
            assert got == reference.process(d, frames), (rate, block)
            if block == 0:
                assert all(frame == (0, 0) for frame in got[: reference.length])
                assert got[reference.length] != (0, 0)

    # New values keep the state; Off and on again, or a new rate, start over.
    publish(chosen)
    d = designed(chosen, 44100)
    reference = Reference(44100)
    frames = noise(count, 20)
    assert run(frames, 44100) == reference.process(d, frames)
    louder = Set(threshold=-6, makeup=0, ratio=4, knee=0, attack=0, release=100)
    publish(louder)
    d = designed(louder, 44100)
    frames = noise(count, 21)
    assert run(frames, 44100) == reference.process(d, frames)
    publish(Set(threshold=0))
    frames = noise(count, 22)
    assert run(frames, 44100) == frames
    publish(louder)
    reference = Reference(44100)
    frames = noise(count, 23)
    assert run(frames, 44100) == reference.process(d, frames)
    d = designed(louder, 32000)
    reference = Reference(32000)
    frames = noise(count, 24)
    assert run(frames, 32000) == reference.process(d, frames)

    # Attack 0 ms, every ratio and both knees: the model agrees at the edges.
    for ratio in range(5):
        for knee in range(2):
            chosen = Set(
                threshold=-24, makeup=1, ratio=ratio, knee=knee, attack=0, release=1000
            )
            publish(Set(threshold=0))
            run(noise(8, 1), 44100)
            publish(chosen)
            d = designed(chosen, 44100)
            reference = Reference(44100)
            frames = noise(count, 30 + ratio) + noise(count, 40 + ratio, 6)
            assert run(frames, 44100) == reference.process(d, frames), (ratio, knee)

    # A full-scale tone above a Limit threshold comes out well below full scale.
    chosen = Set(threshold=-24, makeup=0, ratio=4, knee=0, attack=0, release=100)
    publish(Set(threshold=0))
    run(noise(8, 1), 44100)
    publish(chosen)
    for _ in range(10):
        got = run(sine(4410, 1000, 44100, 32767), 44100)
    peak = max(abs(s) for frame in got for s in frame)
    assert 0 < peak < 0.3 * 32767, peak

    # Auto makeup over a quiet start then full scale: the output saturates
    # rather than wraps, and still matches the model.
    chosen = Set(threshold=-24, makeup=1, ratio=4, knee=1, attack=30, release=500)
    publish(Set(threshold=0))
    run(noise(8, 1), 44100)
    publish(chosen)
    d = designed(chosen, 44100)
    reference = Reference(44100)
    frames = noise(count, 50, 8) + noise(count, 51)
    got = run(frames, 44100)
    assert got == reference.process(d, frames)
    assert reference.clamped > 0
    assert all(-32768 <= s <= 32767 for frame in got for s in frame)


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="compressor-tests-") as name:
    work = Path(name)
    dll = build(
        work,
        "compressor.so",
        [
            root / "host.c",
            sdk / "rv_libc.c",
            root.parent / "compressor.c",
            root.parent / "curve.c",
            root.parent / "design.c",
            root.parent / "labels.c",
        ],
    )
    design_tests(dll)
    label_tests(dll)
    process_tests(dll)
print("Compressor tests passed")
