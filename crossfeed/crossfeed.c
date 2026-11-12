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
 * the pump task takes it over when it sees the generation move and fades the
 * state that played into the new one. Only the pump task touches what plays.
 * Samples are S16 raised by SAMPLE_SHIFT bits, as Rockbox's DSP keeps them, so
 * the Q31 coefficients apply unchanged. */

#include "crossfeed.h"
#include "rv_audio.h"
#include "rv_libc.h"

enum {
    SAMPLE_SHIFT = 12,
    /* Rockbox's 300 us delay, in interleaved int32 entries, at the highest rate. */
    DELAY_ENTRIES = 2 * (300 * 48000 / 1000000),
};

static const struct rv_api *api;

static struct crossfeed_set next;
static struct rv_handoff handoff;

/* Pump task only. */
struct crossfeed_state {
    struct crossfeed_set set;
    int32_t vcl, vcr, vdiff, coef1, coef2;
    int32_t gain, coefs[3];
    int32_t history[4], delay[DELAY_ENTRIES];
    uint32_t delay_index, delay_length;
};
static struct crossfeed_state playing, fading;
static struct rv_fade fade;
static uint32_t rate_playing;

void crossfeed_start(const struct rv_api *core)
{
    api = core;
}

void crossfeed_publish(const struct crossfeed_set *set)
{
    rv_handoff_publish(api, &handoff, &next, set, sizeof next);
}

/* Back to S16, rounded. */
static inline int16_t output(int32_t sample)
{
    return rv_saturate16((sample + (1 << (SAMPLE_SHIFT - 1))) >> SAMPLE_SHIFT);
}

/* Takes over the set the UI task left, fading from what played when the rate
 * is the same. A set left meanwhile moves the generation again, and the next
 * call takes that one. The histories start over when the mode or the rate
 * changes, as in Rockbox; new Custom values keep them. A rate change comes with
 * a track, so it ends a fade instead of starting one. */
static void take(uint32_t rate)
{
    unsigned int was = playing.set.mode, same_rate = rate == rate_playing;
    if (same_rate)
        fading = playing;
    rv_handoff_take(&handoff, &playing.set, &next, sizeof playing.set);
    const struct crossfeed_set *set = &playing.set;
    if (set->mode == CROSSFEED_CUSTOM) {
        struct crossfeed_custom custom;
        crossfeed_design(&custom, rate, set->direct_tenths, set->cross_tenths, set->hf_tenths,
                         set->cutoff_hz, set->precut_tenths);
        playing.gain = custom.gain;
        memcpy(playing.coefs, custom.coefs, sizeof playing.coefs);
    } else if (set->mode == CROSSFEED_SIMPLE) {
        /* 1 / (F.Rforward.C) and 1 / (F.Rcross.C). */
        playing.coef1 = (int32_t)crossfeed_ratio_q31(2128, rate);
        playing.coef2 = (int32_t)crossfeed_ratio_q31(1000, rate);
    }
    if (set->mode != was || !same_rate) {
        playing.vcl = playing.vcr = playing.vdiff = 0;
        memset(playing.history, 0, sizeof playing.history);
        memset(playing.delay, 0, sizeof playing.delay);
        playing.delay_index = 0;
        playing.delay_length = 2 * (300 * rate / 1000000);
    }
    if (same_rate)
        rv_fade_start(&fade, 0);
    else
        fade.remaining = 0;
    rate_playing = rate;
}

static void simple(struct crossfeed_state *state, int16_t *samples, uint32_t frames)
{
    int32_t l = state->vcl, r = state->vcr, diff = state->vdiff;
    const int32_t c1 = state->coef1, c2 = state->coef2;
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int32_t lout = (int32_t)samples[0] * (1 << SAMPLE_SHIFT) + l;
        int32_t rout = (int32_t)samples[1] * (1 << SAMPLE_SHIFT) + r;
        samples[0] = output(lout);
        samples[1] = output(rout);

        int32_t common = rv_fracmul(diff, c2);
        l -= rv_fracmul(l, c1) + common;
        r -= rv_fracmul(r, c1) - common;
        diff = lout - rout;
    }
    state->vcl = l;
    state->vcr = r;
    state->vdiff = diff;
}

static void custom(struct crossfeed_state *state, int16_t *samples, uint32_t frames)
{
    const int32_t g = state->gain, c0 = state->coefs[0], c1 = state->coefs[1],
                  c2 = state->coefs[2];
    int32_t *history = state->history, *delay = state->delay;
    int32_t *di = delay + state->delay_index;
    const int32_t *end = delay + state->delay_length;
    for (int16_t *last = samples + frames * 2; samples < last; samples += 2) {
        int32_t left = (int32_t)samples[0] * (1 << SAMPLE_SHIFT);
        int32_t right = (int32_t)samples[1] * (1 << SAMPLE_SHIFT);

        /* The delayed left sample through the shelf, then into the line. */
        int32_t acc =
            rv_fracmul(di[0], c0) + rv_fracmul(history[0], c1) + rv_fracmul(history[1], c2);
        history[1] = acc;
        history[0] = di[0];
        di[0] = left;
        acc = rv_fracmul(di[1], c0) + rv_fracmul(history[2], c1) + rv_fracmul(history[3], c2);
        history[3] = acc;
        history[2] = di[1];
        di[1] = right;

        samples[0] = output(rv_fracmul(left, g) + history[3]);
        samples[1] = output(rv_fracmul(right, g) + history[1]);

        di += 2;
        if (di >= end)
            di = delay;
    }
    state->delay_index = (uint32_t)(di - delay);
}

static void run(void *state, int16_t *samples, uint32_t frames)
{
    switch (((struct crossfeed_state *)state)->set.mode) {
    case CROSSFEED_SIMPLE:
        simple(state, samples, frames);
        break;
    case CROSSFEED_CUSTOM:
        custom(state, samples, frames);
        break;
    default:
        break;
    }
}

void crossfeed_process(int16_t *samples, uint32_t frames, uint32_t rate)
{
    if (rv_handoff_pending(&handoff) || rate != rate_playing)
        take(rate);
    rv_fade_run(&fade, run, &playing, &fading, samples, frames);
}
