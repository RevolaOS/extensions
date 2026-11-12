#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
Q15 = 1 << 15
WIDTHS = range(0, 255, 5)
MODES = ["Stereo", "Mono", "Left Only", "Right Only", "Karaoke", "Swap", "Custom"]
STEREO, MONO, LEFT, RIGHT, KARAOKE, SWAP, CUSTOM = range(7)


class Set(ctypes.Structure):
    _fields_ = [("mode", ctypes.c_uint8), ("width", ctypes.c_uint8)]


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


def reference_coefficients(percent):
    """Rockbox's width in floating point, as Q15."""
    w = percent / 100
    if percent <= 100:
        gain, cross = (1 + w) / 2, (1 - w) / 2
    else:
        gain = (1 + w) / (2 * w)
        cross = gain - 1
    return gain * Q15, cross * Q15


def rockbox_coefficients(percent):
    """Rockbox's integer design (channel_mode_custom_set_width), as Q15."""
    width = percent * 0x7FFFFF // 100
    if percent <= 100:
        straight = (0x7FFFFF + width) // 2
        cross = straight - width
    else:
        straight = ((0x7FFFFF + width) << 22) // width
        cross = straight - 0x7FFFFF
    return (straight << 8) >> 16, (cross << 8) >> 16


def coefficient_tests(dll):
    coefficients = dll.channels_coefficients
    coefficients.argtypes = [ctypes.c_uint] + [ctypes.POINTER(ctypes.c_int32)] * 2
    coefficients.restype = None

    def designed(percent):
        gain, cross = ctypes.c_int32(), ctypes.c_int32()
        coefficients(percent, gain, cross)
        return gain.value, cross.value

    for percent in WIDTHS:
        gain, cross = designed(percent)
        for actual, expected in zip((gain, cross), reference_coefficients(percent)):
            assert abs(actual - expected) <= 1, (percent, actual, expected)
        for actual, expected in zip((gain, cross), rockbox_coefficients(percent)):
            assert abs(actual - expected) <= 2, (percent, actual, expected)
        assert abs(gain) + abs(cross) <= Q15, (percent, gain, cross)
    assert designed(100) == (Q15, 0)
    assert designed(0) == (Q15 // 2, Q15 // 2)
    assert designed(250) == (22938, -9830)


def label_tests(dll):
    def labels(name, count):
        return [s.decode() for s in (ctypes.c_char_p * count).in_dll(dll, name)]

    assert labels("mode_labels", 7) == MODES
    assert labels("width_labels", 51) == [f"{5 * i} %" for i in range(51)]
    assert labels("width_labels", 51)[20] == "100 %"


def truncate(value):
    """C's division by two: toward zero."""
    return int(value / 2)


def model(mode, frames, gain=0, cross=0):
    out = []
    for left, right in frames:
        if mode == STEREO:
            out.append((left, right))
        elif mode == MONO:
            mid = truncate(left) + truncate(right)
            out.append((mid, mid))
        elif mode == LEFT:
            out.append((left, left))
        elif mode == RIGHT:
            out.append((right, right))
        elif mode == KARAOKE:
            side = truncate(left) - truncate(right)
            out.append((side, -side))
        elif mode == SWAP:
            out.append((right, left))
        else:
            out.append(
                (
                    (left * gain + right * cross + 16384) >> 15,
                    (right * gain + left * cross + 16384) >> 15,
                )
            )
    return out


def noise(count, seed):
    """Independent noise on each channel, with a hard-panned first frame."""
    value = seed
    frames = []
    for _ in range(count):
        pair = []
        for _ in range(2):
            value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
            sample = value >> 16
            pair.append(sample - 65536 if sample >= 32768 else sample)
        frames.append(tuple(pair))
    frames[0] = (32767, 0)
    return frames


def process_tests(dll):
    publish = dll.channels_publish
    publish.argtypes = [ctypes.POINTER(Set)]
    publish.restype = None
    process = dll.channels_process
    process.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_uint32]
    process.restype = None
    coefficients = dll.channels_coefficients
    coefficients.argtypes = [ctypes.c_uint] + [ctypes.POINTER(ctypes.c_int32)] * 2
    coefficients.restype = None
    dll.test_irq_calls.restype = ctypes.c_uint32
    count = 512

    def run(frames):
        buffer = (ctypes.c_int16 * (2 * count))(*[s for frame in frames for s in frame])
        process(buffer, count)
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(count)]

    def designed(percent):
        gain, cross = ctypes.c_int32(), ctypes.c_int32()
        coefficients(percent, gain, cross)
        return gain.value, cross.value

    # Stereo leaves the stream alone; one publish masks interrupts once.
    publish(Set(mode=STEREO))
    frames = noise(count, 1)
    assert run(frames) == frames
    assert dll.test_irq_calls() == 2

    # Each fixed mode against its model.
    for mode in (MONO, LEFT, RIGHT, KARAOKE, SWAP):
        publish(Set(mode=mode))
        frames = noise(count, 2 + mode)
        assert run(frames) == model(mode, frames), MODES[mode]

    # Custom at several widths; 100 % is the input bit for bit.
    for index in (0, 10, 20, 30, 50):
        publish(Set(mode=CUSTOM, width=index))
        frames = noise(count, 20 + index)
        expected = model(CUSTOM, frames, *designed(5 * index))
        assert run(frames) == expected, index
        if index == 20:
            assert expected == frames

    # No width can leave S16: the four corners at every width, unclamped model.
    corners = [(32767, 32767), (32767, -32768), (-32768, 32767), (-32768, -32768)]
    loud = (corners * (count // 4 + 1))[:count]
    for index in range(51):
        publish(Set(mode=CUSTOM, width=index))
        expected = model(CUSTOM, loud, *designed(5 * index))
        assert all(-32768 <= s <= 32767 for frame in expected for s in frame), index
        assert run(loud) == expected, index

    # The latest set wins: one published after a process, and the last of two
    # published before one.
    publish(Set(mode=MONO))
    frames = noise(count, 90)
    assert run(frames) == model(MONO, frames)
    publish(Set(mode=SWAP))
    frames = noise(count, 91)
    assert run(frames) == model(SWAP, frames)
    publish(Set(mode=KARAOKE))
    publish(Set(mode=CUSTOM, width=40))
    frames = noise(count, 92)
    assert run(frames) == model(CUSTOM, frames, *designed(200))
    publish(Set(mode=CUSTOM, width=10))
    frames = noise(count, 93)
    assert run(frames) == model(CUSTOM, frames, *designed(50))


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="channels-tests-") as name:
    work = Path(name)
    sources = [root / "host.c"] + [
        root.parent / name for name in ("channels.c", "labels.c", "modes.c")
    ]
    dll = build(work, "channels.so", sources)
    coefficient_tests(dll)
    label_tests(dll)
    process_tests(dll)
print("Channels tests passed")
