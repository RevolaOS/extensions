/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox lib/rbcodec/dsp/crossfeed.c.
 *
 * Copyright (C) 2006 Thom Johansen
 * Copyright (C) 2010 Bertrik Sikken
 * Copyright (C) 2012 Michael Sevakis
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* Both of Rockbox's crossfeeds on the native interleaved S16 output, in place.
 * Simple is Jan Meier's passive circuit: each channel's filtered difference
 * signal leaks into the other. Custom feeds each channel through a 300 us
 * delay and a first-order high shelf into the other, with the direct path
 * turned down.
 *
 * The UI task leaves a set with interrupts masked and moves the generation;
 * the pump task takes it over when it sees the generation move. Only the pump
 * task touches what plays. Samples are S16 raised by SAMPLE_SHIFT bits, as
 * Rockbox's DSP keeps them, so the Q31 coefficients apply unchanged. */

#include "crossfeed.h"
#include "rv_api.h"
#include "rv_libc.h"

enum {
    SAMPLE_SHIFT = 12,
    /* Rockbox's 300 us delay, in interleaved int32 entries, at the highest rate. */
    DELAY_ENTRIES = 2 * (300 * 48000 / 1000000),
};

static const struct rv_api *api;

static struct crossfeed_set next;
static volatile uint32_t generation;

/* Pump task only. */
static struct crossfeed_set playing;
static uint32_t taken, rate_playing;
static int32_t vcl, vcr, vdiff, coef1, coef2;
static int32_t gain, coefs[3];
static int32_t history[4], delay[DELAY_ENTRIES];
static uint32_t delay_index, delay_length;

void crossfeed_start(const struct rv_api *core)
{
    api = core;
}

void crossfeed_publish(const struct crossfeed_set *set)
{
    uint32_t state = api->irq_save();
    next = *set;
    generation++;
    api->irq_restore(state);
}

static inline int32_t fracmul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 31);
}

static inline int16_t output(int32_t sample)
{
    int32_t value = (sample + (1 << (SAMPLE_SHIFT - 1))) >> SAMPLE_SHIFT;
    return value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
}

/* Takes over the set the UI task left. A set left meanwhile moves the
 * generation again, and the next call takes that one. The histories start over
 * when the mode or the rate changes, as in Rockbox; new Custom values keep them. */
static void take(uint32_t rate)
{
    unsigned int was = playing.mode;
    uint32_t seen;
    do {
        seen = generation;
        playing = next;
    } while (seen != generation);
    taken = seen;
    if (playing.mode == CROSSFEED_CUSTOM) {
        struct crossfeed_custom custom;
        crossfeed_design(&custom, rate, playing.direct_tenths, playing.cross_tenths,
                         playing.hf_tenths, playing.cutoff_hz, playing.precut_tenths);
        gain = custom.gain;
        memcpy(coefs, custom.coefs, sizeof coefs);
    } else if (playing.mode == CROSSFEED_SIMPLE) {
        /* 1 / (F.Rforward.C) and 1 / (F.Rcross.C). */
        coef1 = (int32_t)crossfeed_ratio_q31(2128, rate);
        coef2 = (int32_t)crossfeed_ratio_q31(1000, rate);
    }
    if (playing.mode != was || rate != rate_playing) {
        vcl = vcr = vdiff = 0;
        memset(history, 0, sizeof history);
        memset(delay, 0, sizeof delay);
        delay_index = 0;
        delay_length = 2 * (300 * rate / 1000000);
    }
    rate_playing = rate;
}

static void simple(int16_t *samples, uint32_t frames)
{
    int32_t l = vcl, r = vcr, diff = vdiff;
    const int32_t c1 = coef1, c2 = coef2;
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int32_t lout = (int32_t)samples[0] * (1 << SAMPLE_SHIFT) + l;
        int32_t rout = (int32_t)samples[1] * (1 << SAMPLE_SHIFT) + r;
        samples[0] = output(lout);
        samples[1] = output(rout);

        int32_t common = fracmul(diff, c2);
        l -= fracmul(l, c1) + common;
        r -= fracmul(r, c1) - common;
        diff = lout - rout;
    }
    vcl = l;
    vcr = r;
    vdiff = diff;
}

static void custom(int16_t *samples, uint32_t frames)
{
    const int32_t g = gain, c0 = coefs[0], c1 = coefs[1], c2 = coefs[2];
    int32_t *di = delay + delay_index;
    const int32_t *end = delay + delay_length;
    for (int16_t *last = samples + frames * 2; samples < last; samples += 2) {
        int32_t left = (int32_t)samples[0] * (1 << SAMPLE_SHIFT);
        int32_t right = (int32_t)samples[1] * (1 << SAMPLE_SHIFT);

        /* The delayed left sample through the shelf, then into the line. */
        int32_t acc = fracmul(di[0], c0) + fracmul(history[0], c1) + fracmul(history[1], c2);
        history[1] = acc;
        history[0] = di[0];
        di[0] = left;
        acc = fracmul(di[1], c0) + fracmul(history[2], c1) + fracmul(history[3], c2);
        history[3] = acc;
        history[2] = di[1];
        di[1] = right;

        samples[0] = output(fracmul(left, g) + history[3]);
        samples[1] = output(fracmul(right, g) + history[1]);

        di += 2;
        if (di >= end)
            di = delay;
    }
    delay_index = (uint32_t)(di - delay);
}

void crossfeed_process(int16_t *samples, uint32_t frames, uint32_t rate)
{
    if (taken != generation || rate != rate_playing)
        take(rate);
    switch (playing.mode) {
    case CROSSFEED_SIMPLE:
        simple(samples, frames);
        break;
    case CROSSFEED_CUSTOM:
        custom(samples, frames);
        break;
    default:
        break;
    }
}
