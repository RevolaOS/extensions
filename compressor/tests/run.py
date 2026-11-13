#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import copy
import ctypes
import math
import subprocess
import sys
import tempfile
from itertools import pairwise
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
        + [f"-I{sdk}", f"-I{sdk}/extension", f"-I{root.parent}", "-D_FORTIFY_SOURCE=0"]
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


FADE = 128


def faded(old, new, lead=0):
    """What rv_fade plays after a take: old alone for `lead` frames, then old
    blended into new over FADE frames at weights 1 to FADE, floor-shifted as the
    ARM does, then new."""
    out = []
    for i, (a, b) in enumerate(zip(old, new)):
        weight = min(max(i - lead + 1, 0), FADE)
        out.append(tuple((x * (FADE - weight) + y * weight) >> 7 for x, y in zip(a, b)))
    return out


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

    def designed(chosen, rate):
        d = Design()
        design(d, chosen, rate)
        return d

    class Player:
        """One instance as the engine keeps it: a set at a rate over a Reference,
        or a pass-through while Off."""

        def __init__(self, rate, chosen):
            self.rate, self.chosen, self.on = rate, chosen, chosen.threshold != 0
            self.reference = Reference(rate)
            self.design = designed(chosen, rate) if self.on else None

        def play(self, frames):
            return (
                self.reference.process(self.design, frames) if self.on else list(frames)
            )

    # What plays and, after a take at the same rate, what fades out and for
    # how many frames it plays alone first.
    players = {"new": Player(44100, Set(threshold=0)), "old": None, "lead": 0}

    def switch(chosen, rate=44100):
        """Publishes a set. At the same rate the engine fades from what played,
        and new values while on keep the state; turning on fills the new delay
        line first; a new rate starts over."""
        publish(chosen)
        old, new = players["new"], Player(rate, chosen)
        if rate == old.rate and old.on and new.on:
            new.reference = copy.deepcopy(old.reference)
        players["new"] = new
        players["old"] = old if rate == old.rate else None
        players["lead"] = new.reference.length if new.on and not old.on else 0

    def expected(frames):
        new = players["new"].play(frames)
        old, players["old"] = players["old"], None
        return new if old is None else faded(old.play(frames), new, players["lead"])

    def run(frames, rate, *splits):
        """Processes the frames in one buffer, or in buffers cut at the splits.
        A rate the engine has not seen starts its state over without a fade."""
        if rate != players["new"].rate:
            players["new"] = Player(rate, players["new"].chosen)
            players["old"] = None
        buffer = (ctypes.c_int16 * (2 * len(frames)))(
            *[s for frame in frames for s in frame]
        )
        edges = [0, *splits, len(frames)]
        for first, last in pairwise(edges):
            process(
                ctypes.cast(ctypes.byref(buffer, 4 * first), process.argtypes[0]),
                last - first,
                rate,
            )
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(len(frames))]

    def play(frames, rate, *splits):
        """Runs the frames and checks them against the model, returning them."""
        got = run(frames, rate, *splits)
        assert got == expected(frames), (rate, splits)
        return got

    def interrupts():
        dll.test_irq_calls.restype = ctypes.c_uint32
        return dll.test_irq_calls()

    def turned_off():
        """Off, with the fade out of the delayed samples over."""
        switch(Set(threshold=0))
        play(noise(FADE, 1), 44100)

    # Off leaves the stream alone.
    switch(Set(threshold=0))
    frames = noise(count, 1)
    assert play(frames, 44100) == frames
    assert interrupts() == 2

    # On at three rates, bit for bit against the model across blocks. The
    # first look-ahead's worth of frames is the silence the delay line held;
    # turning on at the rate that plays hides it under the input, and fades in
    # the delayed samples after it.
    chosen = Set(threshold=-12, makeup=1, ratio=1, knee=1, attack=5, release=500)
    for rate in (44100, 48000, 8000):
        switch(chosen, rate)
        length = players["new"].reference.length
        for block in range(3):
            frames = noise(count, block + rate)
            got = play(frames, rate)
            if block == 0 and rate == 44100:
                assert got[:length] == frames[:length]
            elif block == 0:
                assert all(frame == (0, 0) for frame in got[:length])
                assert got[length] != (0, 0)

    # New values keep the state and fade in; Off fades the delayed samples out
    # into the input; on again fills the line, then fades in; a new rate during
    # a fade ends it and starts over.
    switch(chosen)
    play(noise(count, 20), 44100)
    louder = Set(threshold=-6, makeup=0, ratio=4, knee=0, attack=0, release=100)
    switch(louder)
    play(noise(count, 21), 44100)
    switch(Set(threshold=0))
    frames = noise(count, 22)
    got = play(frames, 44100)
    assert got[FADE:] == frames[FADE:] and got[:FADE] != frames[:FADE]
    switch(louder)
    play(noise(64, 23), 44100)
    play(noise(count, 24), 32000)

    # The lead and the fade span buffer edges and end after their frames
    # whatever their size; a set taken during a fade restarts the fade from
    # the set that plays.
    turned_off()
    switch(chosen, 32000)
    play(noise(count, 25), 32000, 17, 100, 300)
    switch(louder, 32000)
    play(noise(64, 26), 32000)
    switch(chosen, 32000)
    play(noise(count, 27), 32000)

    # Attack 0 ms, every ratio and both knees: the model agrees at the edges.
    for ratio in range(5):
        for knee in range(2):
            chosen = Set(
                threshold=-24, makeup=1, ratio=ratio, knee=knee, attack=0, release=1000
            )
            turned_off()
            switch(chosen)
            frames = noise(count, 30 + ratio) + noise(count, 40 + ratio, 6)
            play(frames, 44100)

    # A full-scale tone above a Limit threshold comes out well below full scale.
    chosen = Set(threshold=-24, makeup=0, ratio=4, knee=0, attack=0, release=100)
    turned_off()
    switch(chosen)
    for _ in range(10):
        got = play(sine(4410, 1000, 44100, 32767), 44100)
    peak = max(abs(s) for frame in got for s in frame)
    assert 0 < peak < 0.3 * 32767, peak

    # Auto makeup over a quiet start then full scale: the output saturates
    # rather than wraps, and still matches the model.
    chosen = Set(threshold=-24, makeup=1, ratio=4, knee=1, attack=30, release=500)
    turned_off()
    switch(chosen)
    frames = noise(count, 50, 8) + noise(count, 51)
    got = play(frames, 44100)
    assert players["new"].reference.clamped > 0
    assert all(-32768 <= s <= 32767 for frame in got for s in frame)


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="compressor-tests-") as name:
    work = Path(name)
    dll = build(
        work,
        "compressor.so",
        [
            root / "host.c",
            sdk / "extension/rv_libc.c",
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
