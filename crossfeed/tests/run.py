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
Q31 = 1 << 31
SHIFT = 12
RATES = (44100, 48000, 32000, 22050, 8000)


class Custom(ctypes.Structure):
    _fields_ = [("gain", ctypes.c_int32), ("coefs", ctypes.c_int32 * 3)]


class Set(ctypes.Structure):
    _fields_ = [
        ("mode", ctypes.c_uint8),
        ("direct", ctypes.c_int16),
        ("cross", ctypes.c_int16),
        ("hf", ctypes.c_int16),
        ("cutoff", ctypes.c_uint16),
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


def reference_design(rate, direct, cross, hf, cutoff):
    """Rockbox's custom crossfeed in floating point: a bilinear first-order
    high shelf with its cutoff moved to the -3 dB point, scaled by the cross
    gain; gains in tenths of a decibel."""
    scaler = 10 ** (cross / 200)
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
    design.argtypes += [ctypes.c_uint]
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
            design(out, rate, direct, cross, hf, cutoff)
            gain, coefs = reference_design(rate, direct, cross, hf, cutoff)
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
    design(out, 44100, 0, -60, -160, 700)
    assert out.gain == Q31 - 1


def label_tests(dll):
    def labels(name, count):
        return [s.decode() for s in (ctypes.c_char_p * count).in_dll(dll, name)]

    def decibels(first, count):
        return [f"{(first + 5 * i) / 10:.1f} dB" for i in range(count)]

    assert labels("direct_labels", 13) == decibels(-60, 13)
    assert labels("cross_labels", 19) == decibels(-120, 19)
    assert labels("hf_labels", 37) == decibels(-240, 37)
    assert labels("cutoff_labels", 16) == [f"{500 + 100 * i} Hz" for i in range(16)]
    assert labels("direct_labels", 13)[9] == "-1.5 dB"
    assert labels("cross_labels", 19)[12] == "-6.0 dB"
    assert labels("hf_labels", 37)[16] == "-16.0 dB"
    assert labels("cutoff_labels", 16)[2] == "700 Hz"


def response_tests(dll):
    """The editor's curve: the cross gain in the bass, the attenuation in the
    treble, falling in between, with the knee near the cutoff."""
    response = dll.crossfeed_response
    response.argtypes = [ctypes.POINTER(Set), ctypes.c_uint]
    response.argtypes += [ctypes.POINTER(ctypes.c_int16), ctypes.c_uint]
    response.restype = None
    points = 261
    cases = [
        (-15, -60, -160, 700),
        (0, -30, -60, 500),
        (-60, -120, -240, 2000),
        (0, -120, -60, 2000),
    ]
    for direct, cross, hf, cutoff in cases:
        curve = (ctypes.c_int16 * points)()
        bank = Set(mode=2, direct=direct, cross=cross, hf=hf, cutoff=cutoff)
        response(bank, 44100, curve, points)
        curve = list(curve)
        assert abs(curve[0] - cross) <= 3, (cross, curve[0])
        assert abs(curve[-1] - hf) <= 3, (hf, curve[-1])
        steps = [b - a for a, b in zip(curve, curve[1:])]
        assert all(s <= 0 for s in steps) if hf < cross else all(s >= 0 for s in steps)
        # The shelf's pole and zero sit either side of the cutoff divided by
        # the half gain g, so the curve is halfway between its ends there.
        hertz = [20 * 1000 ** (i / (points - 1)) for i in range(points)]
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


def process_tests(dll):
    publish = dll.crossfeed_publish
    publish.argtypes = [ctypes.POINTER(Set)]
    publish.restype = None
    process = dll.crossfeed_process
    process.argtypes = [ctypes.POINTER(ctypes.c_int16)] + [ctypes.c_uint32] * 2
    process.restype = None
    design = dll.crossfeed_design
    design.argtypes = [ctypes.POINTER(Custom), ctypes.c_uint] + [ctypes.c_int] * 3
    design.argtypes += [ctypes.c_uint]
    design.restype = None
    count = 512

    def run(frames, rate):
        buffer = (ctypes.c_int16 * (2 * count))(*[s for frame in frames for s in frame])
        process(buffer, count, rate)
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(count)]

    def interrupts(dll):
        dll.test_irq_calls.restype = ctypes.c_uint32
        return dll.test_irq_calls()

    # Off leaves the stream alone.
    publish(Set(mode=0))
    frames = noise(count, 1, True)
    assert run(frames, 44100) == frames
    assert interrupts(dll) == 2

    # Simple, bit for bit against the reference, history carried across
    # blocks, restarted by a rate change; mono passes through unchanged.
    publish(Set(mode=1))
    for rate in (44100, 48000, 8000):
        reference = Reference(rate)
        for block in range(3):
            frames = noise(count, block + rate, True)
            assert run(frames, rate) == reference.simple(frames), (rate, block)
    frames = noise(count, 7, False)
    assert run(frames, 44100) == frames

    # Custom at every rate, the delay line and shelf history carried across
    # blocks; the panned impulse reaches the other channel delayed.
    def custom_set(direct, cross, hf, cutoff):
        return Set(mode=2, direct=direct, cross=cross, hf=hf, cutoff=cutoff)

    def designed(rate, bank):
        out = Custom()
        design(out, rate, bank.direct, bank.cross, bank.hf, bank.cutoff)
        return out.gain, list(out.coefs)

    for rate in RATES:
        bank = custom_set(-15, -60, -160, 700)
        publish(bank)
        gain, coefs = designed(rate, bank)
        reference = Reference(rate)
        for block in range(3):
            frames = noise(count, block + rate, True)
            got = run(frames, rate)
            assert got == reference.custom(frames, gain, coefs), (rate, block)
            if block == 0:
                delay = 300 * rate // 1000000
                assert all(right == 0 for _, right in got[:delay])
                assert got[delay][1] != 0

    # New Custom values keep the delay line and shelf history; a change of
    # mode starts them over.
    publish(Set(mode=1))
    run(noise(count, 9, True), 44100)
    first, second = custom_set(-15, -60, -160, 700), custom_set(0, -30, -60, 500)
    publish(first)
    reference = Reference(44100)
    frames = noise(count, 10, True)
    assert run(frames, 44100) == reference.custom(frames, *designed(44100, first))
    publish(second)
    frames = noise(count, 11, True)
    assert run(frames, 44100) == reference.custom(frames, *designed(44100, second))
    publish(Set(mode=1))
    frames = noise(count, 12, True)
    assert run(frames, 44100) == Reference(44100).simple(frames)

    # Correlated full-scale input saturates instead of wrapping in Custom.
    publish(custom_set(0, -30, -60, 500))
    loud = [(32767, 32767)] * count
    got = run(loud, 44100)
    assert got[-1] == (32767, 32767), got[-1]
    assert all(-32768 <= s <= 32767 for frame in got for s in frame)


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="crossfeed-tests-") as name:
    work = Path(name)
    sources = [root / "host.c"] + [
        root.parent / name
        for name in ("crossfeed.c", "design.c", "labels.c", "response.c")
    ]
    dll = build(work, "crossfeed.so", sources)
    design_tests(dll)
    label_tests(dll)
    response_tests(dll)
    process_tests(dll)
print("Crossfeed tests passed")
