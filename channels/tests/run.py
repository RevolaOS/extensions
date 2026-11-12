#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import ctypes
import subprocess
import sys
import tempfile
from itertools import pairwise
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

    label = dll.channels_label
    label.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
    label.restype = ctypes.c_char_p
    out = ctypes.create_string_buffer(8)
    assert labels("mode_labels", 7) == MODES
    assert [label(0, i, out).decode() for i in range(7)] == MODES
    widths = [label(1, i, out).decode() for i in range(51)]
    assert widths == [f"{5 * i} %" for i in range(51)]
    assert widths[20] == "100 %"


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

    def run(frames, *splits):
        """Processes the frames in one buffer, or in buffers cut at the splits."""
        buffer = (ctypes.c_int16 * (2 * len(frames)))(
            *[s for frame in frames for s in frame]
        )
        edges = [0, *splits, len(frames)]
        for first, last in pairwise(edges):
            process(
                ctypes.cast(ctypes.byref(buffer, 4 * first), process.argtypes[0]),
                last - first,
            )
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(len(frames))]

    def designed(percent):
        gain, cross = ctypes.c_int32(), ctypes.c_int32()
        coefficients(percent, gain, cross)
        return gain.value, cross.value

    # What plays and, after a take, what fades out: (mode, gain, cross).
    players = {"new": (STEREO, 0, 0), "old": None}

    def switch(mode, width=0):
        """Publishes a set; `expected` then fades from the mode that played."""
        publish(Set(mode=mode, width=width))
        players["old"] = players["new"]
        players["new"] = (
            (mode, *designed(5 * width)) if mode == CUSTOM else (mode, 0, 0)
        )

    def expected(frames):
        old, players["old"] = players["old"], None
        new = players["new"]
        return faded(model(old[0], frames, *old[1:]), model(new[0], frames, *new[1:]))

    def play(frames, *splits):
        """Runs the frames and checks them against the model, returning them."""
        got = run(frames, *splits)
        assert got == expected(frames), splits
        return got

    # Stereo leaves the stream alone; one publish masks interrupts once.
    switch(STEREO)
    frames = noise(count, 1)
    assert play(frames) == frames
    assert dll.test_irq_calls() == 2

    # Each fixed mode against its model, faded in from the last.
    for mode in (MONO, LEFT, RIGHT, KARAOKE, SWAP):
        switch(mode)
        frames = noise(count, 2 + mode)
        play(frames)

    # Custom at several widths; 100 % is the input bit for bit.
    for index in (0, 10, 20, 30, 50):
        switch(CUSTOM, index)
        frames = noise(count, 20 + index)
        play(frames)
        if index == 20:
            assert model(CUSTOM, frames, *designed(100)) == frames

    # No width can leave S16: the four corners at every width, unclamped model.
    corners = [(32767, 32767), (32767, -32768), (-32768, 32767), (-32768, -32768)]
    loud = (corners * (count // 4 + 1))[:count]
    for index in range(51):
        switch(CUSTOM, index)
        plain = model(CUSTOM, loud, *designed(5 * index))
        assert all(-32768 <= s <= 32767 for frame in plain for s in frame), index
        play(loud)

    # The latest set wins: one published after a process, and the last of two
    # published before one, which fades from what played, not from the skipped set.
    switch(MONO)
    frames = noise(count, 90)
    play(frames)
    switch(SWAP)
    frames = noise(count, 91)
    play(frames)
    publish(Set(mode=KARAOKE))
    switch(CUSTOM, 40)
    frames = noise(count, 92)
    play(frames)

    # A fade spans buffer edges, and ends after FADE frames whatever their size.
    switch(CUSTOM, 10)
    frames = noise(count, 93)
    play(frames, 17, 100)

    # A set taken during a fade restarts the fade from the set that plays.
    switch(KARAOKE)
    frames = noise(64, 94)
    play(frames)
    switch(LEFT)
    frames = noise(count, 95)
    play(frames)


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="channels-tests-") as name:
    work = Path(name)
    sources = [root / "host.c", sdk / "rv_libc.c"] + [
        root.parent / name for name in ("channels.c", "labels.c", "modes.c")
    ]
    dll = build(work, "channels.so", sources)
    coefficient_tests(dll)
    label_tests(dll)
    process_tests(dll)
print("Channels tests passed")
