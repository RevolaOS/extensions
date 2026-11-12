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
Q31 = 1 << 31
SHIFT = 12
RATES = (44100, 48000, 32000, 22050, 8000)
AUTO = 1  # CROSSFEED_PRECUT_AUTO
DIRECTS = range(-60, 1, 5)
CROSSES = range(-120, -29, 5)


class Custom(ctypes.Structure):
    _fields_ = [
        ("gain", ctypes.c_int32),
        ("coefs", ctypes.c_int32 * 3),
        ("precut", ctypes.c_int16),
    ]


class Set(ctypes.Structure):
    _fields_ = [
        ("mode", ctypes.c_uint8),
        ("direct", ctypes.c_int16),
        ("cross", ctypes.c_int16),
        ("hf", ctypes.c_int16),
        ("cutoff", ctypes.c_uint16),
        ("precut", ctypes.c_int16),
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


def to_q31(value):
    if value >= 1:
        return Q31 - 1
    return int(math.floor(value * Q31 + 0.5))


def auto_precut(direct, cross):
    """The smallest half-decibel step that keeps direct + cross at or below
    0 dB, in tenths."""
    total = 10 ** (direct / 200) + 10 ** (cross / 200)
    return -5 * max(0, math.ceil(40 * math.log10(total)))


def reference_design(rate, direct, cross, hf, cutoff, precut=0):
    """Rockbox's custom crossfeed in floating point: a bilinear first-order
    high shelf with its cutoff moved to the -3 dB point, scaled by the cross
    gain; gains in tenths of a decibel. The precut lowers both paths."""
    if precut == AUTO:
        precut = auto_precut(direct, cross)
    direct += precut
    scaler = 10 ** ((cross + precut) / 200)
    g = 10 ** ((hf - cross) / 400)
    theta = min(math.pi * cutoff / (g * rate), 0.45 * math.pi)  # held below Nyquist
    s, c = math.sin(theta), math.cos(theta)
    b0, b1, a0, a1 = s + c * g, s - c * g, s + c / g, s - c / g
    return to_q31(10 ** (direct / 200)), [
        to_q31(scaler * b0 / a0),
        to_q31(scaler * b1 / a0),
        to_q31(-a1 / a0),
    ]


def design_tests(dll):
    design = dll.crossfeed_design
    design.argtypes = [ctypes.POINTER(Custom), ctypes.c_uint] + [ctypes.c_int] * 3
    design.argtypes += [ctypes.c_uint, ctypes.c_int]
    design.restype = None
    cases = [
        (-15, -60, -160, 700),
        (0, -30, -60, 500),
        (-60, -120, -240, 2000),
        (0, -120, -60, 2000),  # the steepest lift: +6 dB across the shelf
        (-5, -30, -240, 500),  # the deepest cut: -21 dB across the shelf
        (-5, -30, -240, 2000),  # the widest angle: above Nyquist at 8 kHz
    ]
    for rate in RATES:
        for direct, cross, hf, cutoff in cases:
            out = Custom()
            design(out, rate, direct, cross, hf, cutoff, 0)
            gain, coefs = reference_design(rate, direct, cross, hf, cutoff)
            assert out.precut == 0
            # The gain table is s3.28.
            assert abs(out.gain - gain) <= 8, (rate, direct, out.gain, gain)
            for actual, expected in zip(out.coefs, coefs, strict=True):
                where = (rate, cross, hf, cutoff, actual, expected)
                assert abs(actual - expected) <= 1024, where
            # The cross path is the cross gain at DC and the attenuation at Nyquist.
            b0, b1, a1 = (c / Q31 for c in out.coefs)
            at_dc = (b0 + b1) / (1 - a1)
            at_nyquist = (b0 - b1) / (1 + a1)
            assert abs(20 * math.log10(at_dc) - cross / 10) < 0.01
            if math.pi * cutoff / (10 ** ((hf - cross) / 400) * rate) < 0.45 * math.pi:
                assert abs(20 * math.log10(at_nyquist) - hf / 10) < 0.01
            assert abs(out.coefs[2]) < Q31 * 0.95  # a stable pole
            # Everything stays below full scale, as the Q31 format needs.
            assert all(abs(c) < Q31 for c in out.coefs)
    out = Custom()
    design(out, 44100, 0, -60, -160, 700, 0)
    assert out.gain == Q31 - 1

    # Auto at every Direct and Cross: the float rule's step, both paths
    # lowered by it, and a centred passage at or below full scale in the bass.
    for direct in DIRECTS:
        for cross in CROSSES:
            out = Custom()
            design(out, 44100, direct, cross, -160, 700, AUTO)
            precut = auto_precut(direct, cross)
            assert out.precut == precut, (direct, cross, out.precut, precut)
            gain, coefs = reference_design(44100, direct, cross, -160, 700, precut)
            # Two s3.28 table values multiplied truncate one more step.
            assert abs(out.gain - gain) <= 16, (direct, cross, out.gain, gain)
            for actual, expected in zip(out.coefs, coefs, strict=True):
                assert abs(actual - expected) <= 1024, (direct, cross, actual, expected)
            b0, b1, a1 = (c / Q31 for c in out.coefs)
            assert out.gain / Q31 + (b0 + b1) / (1 - a1) <= 1.0 + 1e-6
    # A fixed precut lowers both paths by exactly that.
    out = Custom()
    design(out, 44100, -15, -60, -160, 700, -30)
    assert out.precut == -30
    gain, coefs = reference_design(44100, -15, -60, -160, 700, -30)
    assert abs(out.gain - gain) <= 16
    assert all(abs(a - e) <= 1024 for a, e in zip(out.coefs, coefs, strict=True))
    assert auto_precut(-15, -60) == -30 and auto_precut(0, -30) == -50


def label_tests(dll):
    label = dll.crossfeed_label
    label.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
    label.restype = ctypes.c_char_p
    MODE, PRECUT, DIRECT, CROSS, HF, CUTOFF = range(6)

    def labels(parameter, count):
        out = ctypes.create_string_buffer(16)
        return [label(parameter, i, out).decode() for i in range(count)]

    def decibels(first, count):
        return [f"{(first + 5 * i) / 10:.1f} dB" for i in range(count)]

    assert labels(MODE, 3) == ["Off", "Simple", "Custom"]
    assert labels(DIRECT, 13) == decibels(-60, 13)
    assert labels(CROSS, 19) == decibels(-120, 19)
    assert labels(HF, 37) == decibels(-240, 37)
    assert labels(CUTOFF, 16) == [
        f"{hz} Hz" if hz < 1000 else f"{hz / 1000:g}k Hz"
        for hz in range(500, 2001, 100)
    ]
    assert labels(DIRECT, 13)[9] == "-1.5 dB"
    assert labels(CROSS, 19)[12] == "-6.0 dB"
    assert labels(HF, 37)[16] == "-16.0 dB"
    assert labels(CUTOFF, 16)[2] == "700 Hz"
    assert labels(PRECUT, 14) == ["Auto"] + decibels(0, 1) + [
        f"{-(i + 1) / 2:.1f} dB" for i in range(12)
    ]


def response_tests(dll):
    """The editor's curves: in Custom the cross gain in the bass, the
    attenuation in the treble, falling in between with the knee near the
    cutoff, over a flat own channel; in Simple the loop's transfer function;
    nothing fed across while Off."""
    response = dll.crossfeed_response
    response.argtypes = [ctypes.POINTER(Set), ctypes.c_uint]
    response.argtypes += [ctypes.POINTER(ctypes.c_int16)] * 2 + [ctypes.c_uint]
    response.restype = None
    points = 261
    hertz = [20 * 1000 ** (i / (points - 1)) for i in range(points)]

    def curves(bank):
        same = (ctypes.c_int16 * points)()
        other = (ctypes.c_int16 * points)()
        response(bank, 44100, same, other, points)
        return list(same), list(other)

    cases = [
        (-15, -60, -160, 700),
        (0, -30, -60, 500),
        (-60, -120, -240, 2000),
        (0, -120, -60, 2000),
    ]
    for direct, cross, hf, cutoff in cases:
        bank = Set(mode=2, direct=direct, cross=cross, hf=hf, cutoff=cutoff)
        same, curve = curves(bank)
        assert same == [direct] * points
        assert abs(curve[0] - cross) <= 3, (cross, curve[0])
        assert abs(curve[-1] - hf) <= 3, (hf, curve[-1])
        steps = [b - a for a, b in zip(curve, curve[1:])]
        assert all(s <= 0 for s in steps) if hf < cross else all(s >= 0 for s in steps)
        # The shelf's pole and zero sit either side of the cutoff divided by
        # the half gain g, so the curve is halfway between its ends there.
        g = 10 ** ((hf - cross) / 400)
        halfway = abs(hf - cross) / 2
        knee = next(i for i, t in enumerate(curve) if abs(t - cross) >= halfway)
        assert 0.8 < hertz[knee] * g / cutoff < 1.25, (cutoff / g, hertz[knee])
        # Against the float reference's coefficients at every point.
        gain, coefs = reference_design(44100, direct, cross, hf, cutoff)
        b0, b1, a1 = (c / Q31 for c in coefs)
        for hz, tenths in zip(hertz, curve):
            w = 2 * math.pi * hz / 44100
            z = complex(math.cos(w), -math.sin(w))
            expected = 200 * math.log10(abs((b0 + b1 * z) / (1 - a1 * z)))
            assert abs(tenths - expected) <= 1, (hz, tenths, expected)

    # The precut is the design's, and lowers both curves by it.
    design = dll.crossfeed_design
    for direct in DIRECTS:
        for cross in CROSSES:
            bank = Set(mode=2, direct=direct, cross=cross, hf=-160, cutoff=700)
            flat, plain = curves(bank)
            assert flat == [direct] * points
            for precut in (AUTO, -30, -60):
                out = Custom()
                design(out, 44100, direct, cross, -160, 700, precut)
                bank.precut = precut
                same, curve = curves(bank)
                assert same == [direct + out.precut] * points, (direct, precut, same[0])
                assert all(
                    abs(c - (f + out.precut)) <= 1
                    for c, f in zip(curve, plain, strict=True)
                )

    # Simple: the loop's transfer function, H for the channel fed across and
    # 1 - H for the ear's own, checked against the loop itself with a tone in
    # one channel and then at every point.
    c1, c2 = 2128 / 44100, 1000 / 44100

    def simple_response(hz):
        w = 2 * math.pi * hz / 44100
        z = complex(math.cos(w), -math.sin(w))
        return c2 * z**2 / (1 - (1 - c1) * z + 2 * c2 * z**2)

    for hz in (50, 1000, 10000):
        count = 20000
        frames = [
            (int(20000 * math.sin(2 * math.pi * hz * i / 44100)), 0)
            for i in range(count)
        ]
        out = Reference(44100).simple(frames)[count // 2 :]
        h = simple_response(hz)
        for amplitude, expected in (
            (max(abs(r) for _, r in out), abs(h)),
            (max(abs(l) for l, _ in out), abs(1 - h)),
        ):
            got = 20 * math.log10(amplitude / 20000)
            assert abs(got - 20 * math.log10(expected)) < 0.1, (hz, got, expected)
    same, other = curves(Set(mode=1))
    for hz, s, o in zip(hertz, same, other):
        h = simple_response(hz)
        assert abs(o - 200 * math.log10(abs(h))) <= 1, (hz, o)
        assert abs(s - 200 * math.log10(abs(1 - h))) <= 1, (hz, s)
    assert abs(other[0] + 123) <= 1 and abs(same[0] + 24) <= 1

    # Off: the ear's own channel only.
    same, other = curves(Set(mode=0))
    assert same == [0] * points
    assert other == [-32768] * points


def wrap(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def fracmul(a, b):
    return wrap((a * b) >> 31)


def output(sample):
    value = (sample + (1 << (SHIFT - 1))) >> SHIFT
    return max(-32768, min(32767, value))


class Reference:
    """Rockbox's two process loops on S16 raised by SHIFT bits, integer for
    integer, with fp_div coefficients from a 64-bit divide."""

    def __init__(self, rate):
        self.coef1 = (2128 << 31) // rate
        self.coef2 = (1000 << 31) // rate
        self.vcl = self.vcr = self.vdiff = 0
        self.history = [0, 0, 0, 0]
        self.delay = [0] * (2 * (300 * rate // 1000000))
        self.index = 0

    def simple(self, frames):
        out = []
        for left, right in frames:
            lout = wrap((left << SHIFT) + self.vcl)
            rout = wrap((right << SHIFT) + self.vcr)
            out.append((output(lout), output(rout)))
            common = fracmul(self.vdiff, self.coef2)
            self.vcl = wrap(self.vcl - (fracmul(self.vcl, self.coef1) + common))
            self.vcr = wrap(self.vcr - (fracmul(self.vcr, self.coef1) - common))
            self.vdiff = wrap(lout - rout)
        return out

    def custom(self, frames, gain, coefs):
        out = []
        h, d = self.history, self.delay
        for left, right in frames:
            left <<= SHIFT
            right <<= SHIFT
            i = self.index
            b0, b1, a1 = coefs
            acc = fracmul(d[i], b0) + fracmul(h[0], b1) + fracmul(h[1], a1)
            h[1], h[0], d[i] = wrap(acc), d[i], left
            acc = fracmul(d[i + 1], b0) + fracmul(h[2], b1) + fracmul(h[3], a1)
            h[3], h[2], d[i + 1] = wrap(acc), d[i + 1], right
            lout = output(fracmul(left, gain) + h[3])
            rout = output(fracmul(right, gain) + h[1])
            out.append((lout, rout))
            self.index = (i + 2) % len(d)
        return out


def noise(count, seed, panned):
    value = seed
    frames = []
    for _ in range(count):
        value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
        left = value >> 16
        left = left - 65536 if left >= 32768 else left
        frames.append((left, 0 if panned else left))
    if panned:
        frames[0] = (32767, 0)
    return frames


FADE = 128


def faded(old, new):
    """What rv_fade plays after a take: old blended into new over the first FADE
    frames at weights 1 to FADE, floor-shifted as C's arithmetic shift does, then new."""
    out = []
    for i, (a, b) in enumerate(zip(old, new)):
        weight = min(i + 1, FADE)
        out.append(tuple((x * (FADE - weight) + y * weight) >> 7 for x, y in zip(a, b)))
    return out


def process_tests(dll):
    publish = dll.crossfeed_publish
    publish.argtypes = [ctypes.POINTER(Set)]
    publish.restype = None
    process = dll.crossfeed_process
    process.argtypes = [ctypes.POINTER(ctypes.c_int16)] + [ctypes.c_uint32] * 2
    process.restype = None
    design = dll.crossfeed_design
    design.argtypes = [ctypes.POINTER(Custom), ctypes.c_uint] + [ctypes.c_int] * 3
    design.argtypes += [ctypes.c_uint, ctypes.c_int]
    design.restype = None
    count = 512

    def designed(rate, bank):
        out = Custom()
        design(out, rate, bank.direct, bank.cross, bank.hf, bank.cutoff, bank.precut)
        return out.gain, list(out.coefs)

    class Player:
        """One instance as the engine keeps it: a set at a rate over a Reference."""

        def __init__(self, rate, chosen):
            self.rate, self.chosen, self.mode = rate, chosen, chosen.mode
            self.reference = Reference(rate)
            self.custom = designed(rate, chosen) if chosen.mode == 2 else None

        def play(self, frames):
            if self.mode == 1:
                return self.reference.simple(frames)
            if self.mode == 2:
                return self.reference.custom(frames, *self.custom)
            return list(frames)

    # What plays and, after a take at the same rate, what fades out.
    players = {"new": Player(44100, Set(mode=0)), "old": None}

    def switch(chosen, rate=44100):
        """Publishes a set. At the same rate the engine fades from what played
        and a same-mode set keeps the history; a new rate starts over."""
        publish(chosen)
        old, new = players["new"], Player(rate, chosen)
        if rate == old.rate and chosen.mode == old.mode:
            new.reference = copy.deepcopy(old.reference)
        players["new"], players["old"] = new, old if rate == old.rate else None

    def expected(frames):
        new = players["new"].play(frames)
        old, players["old"] = players["old"], None
        return new if old is None else faded(old.play(frames), new)

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

    def interrupts(dll):
        dll.test_irq_calls.restype = ctypes.c_uint32
        return dll.test_irq_calls()

    # Off leaves the stream alone.
    switch(Set(mode=0))
    frames = noise(count, 1, True)
    assert play(frames, 44100) == frames
    assert interrupts(dll) == 2

    # Simple, bit for bit against the reference, history carried across
    # blocks, restarted by a rate change; mono passes through unchanged.
    switch(Set(mode=1))
    for rate in (44100, 48000, 8000):
        for block in range(3):
            frames = noise(count, block + rate, True)
            play(frames, rate)
    frames = noise(count, 7, False)
    assert play(frames, 44100) == frames

    # Custom at every rate, the delay line and shelf history carried across
    # blocks; the panned impulse reaches the other channel delayed, under the
    # fade from what played before.
    def custom_set(direct, cross, hf, cutoff, precut=0):
        return Set(
            mode=2, direct=direct, cross=cross, hf=hf, cutoff=cutoff, precut=precut
        )

    for rate in RATES:
        bank = custom_set(-15, -60, -160, 700)
        switch(bank, rate)
        for block in range(3):
            frames = noise(count, block + rate, True)
            play(frames, rate)
            if block == 0:
                alone = Player(rate, bank).play(frames)
                delay = 300 * rate // 1000000
                assert all(right == 0 for _, right in alone[:delay])
                assert alone[delay][1] != 0

    # New Custom values keep the delay line and shelf history, and fade in from
    # the old ones; a change of mode fades from the old mode into fresh state.
    switch(Set(mode=1))
    frames = noise(count, 9, True)
    play(frames, 44100)
    first, second = custom_set(-15, -60, -160, 700), custom_set(0, -30, -60, 500)
    switch(first)
    frames = noise(count, 10, True)
    play(frames, 44100)
    switch(second)
    frames = noise(count, 11, True)
    play(frames, 44100)
    switch(Set(mode=1))
    frames = noise(count, 12, True)
    play(frames, 44100)

    # A fade spans buffer edges and ends after FADE frames whatever their size;
    # a set taken during a fade restarts the fade from the set that plays; a
    # rate change ends a fade and starts the state over.
    switch(first)
    frames = noise(count, 15, True)
    play(frames, 44100, 17, 100)
    switch(second)
    frames = noise(64, 16, True)
    play(frames, 44100)
    switch(Set(mode=1))
    frames = noise(count, 17, True)
    play(frames, 44100)
    switch(first)
    frames = noise(64, 18, True)
    play(frames, 44100)
    frames = noise(count, 19, True)
    play(frames, 48000)

    # Correlated full-scale input saturates instead of wrapping in Custom
    # without a precut.
    switch(custom_set(0, -30, -60, 500))
    loud = [(32767, 32767)] * count
    got = play(loud, 44100)
    assert got[-1] == (32767, 32767), got[-1]
    assert all(-32768 <= s <= 32767 for frame in got for s in frame)

    # A full-scale centred bass tone at the defaults clips without a precut
    # and stays inside full scale with Auto, and the Auto set plays bit for
    # bit as the reference with its design.
    def tone():
        return [
            (s, s)
            for s in (
                round(32767 * math.sin(2 * math.pi * 60 * i / 44100))
                for i in range(count)
            )
        ]

    switch(custom_set(-15, -60, -160, 700))
    got = play(tone(), 44100)
    assert sum(s in (32767, -32768) for frame in got for s in frame) > count // 4
    bank = custom_set(-15, -60, -160, 700, AUTO)
    switch(bank)
    play(tone(), 44100)  # the shelf and delay settle
    got = play(tone(), 44100)
    assert all(abs(s) < 32767 for frame in got for s in frame)
    switch(Set(mode=1))
    frames = noise(count, 13, True)
    play(frames, 44100)
    switch(bank)
    frames = noise(count, 14, True)
    play(frames, 44100)


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="crossfeed-tests-") as name:
    work = Path(name)
    sources = [root / "host.c", sdk / "rv_libc.c", sdk / "rv_math.c"] + [
        root.parent / name
        for name in ("crossfeed.c", "design.c", "labels.c", "modes.c", "response.c")
    ]
    dll = build(work, "crossfeed.so", sources)
    design_tests(dll)
    label_tests(dll)
    response_tests(dll)
    process_tests(dll)
print("Crossfeed tests passed")
