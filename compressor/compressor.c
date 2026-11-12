/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox lib/rbcodec/dsp/compressor.c.
 *
 * Copyright (C) 2009 Jeffrey Goode
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* Rockbox's compressor on the native interleaved S16 output, in place. The
 * channels' mean goes through two high-pass filters that weight the treble,
 * the result picks a gain from the curve, and that gain follows a one-pole
 * attack or release. The gain applies to the samples of 3 ms ago, held in a
 * delay line, so a limiter can pull the gain down before a peak plays.
 *
 * The UI task leaves a set with interrupts masked and moves the generation;
 * the pump task takes it over when it sees the generation move and fades the
 * state that played into the new one. Only the pump task touches what plays.
 * Samples are S16 raised to Rockbox's 28 fractional bits, so its S7.24 gains
 * and shifts apply unchanged. */

#include "compressor.h"
#include "rv_audio.h"
#include "rv_libc.h"

enum {
    SAMPLE_SHIFT = 13, /* S16 to S3.28 */
    UNITY = 1 << 24,
    DELAY_MS = 3,
    DELAY_FRAMES = DELAY_MS * 48000 / 1000, /* at the highest rate */
};

static const struct rv_api *api;

static struct compressor_set next;
static struct rv_handoff handoff;

/* Pump task only. */
struct compressor_state {
    struct compressor_set set;
    struct compressor_design design;
    int32_t release_gain, release_holdoff;
    int32_t hp_x1, hp1_y1, hp2_y1;
    /* The delay line is exactly the look-ahead long: each frame reads the one
     * it replaces. */
    int16_t delay[DELAY_FRAMES][2];
    uint32_t delay_index, delay_length;
};
static struct compressor_state playing, fading;
static struct rv_fade fade;
static uint32_t rate_playing;

void compressor_start(const struct rv_api *core)
{
    api = core;
}

void compressor_publish(const struct compressor_set *set)
{
    rv_handoff_publish(api, &handoff, &next, set, sizeof next);
}

/* a (S7.24) times b. */
static inline int32_t scale(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 24);
}

static inline int32_t magnitude(int32_t sample)
{
    return sample < 0 ? -(sample + 1) : sample;
}

static void reset(struct compressor_state *state, uint32_t rate)
{
    state->release_gain = UNITY;
    state->hp_x1 = state->hp1_y1 = state->hp2_y1 = 0;
    memset(state->delay, 0, sizeof state->delay);
    state->delay_index = 0;
    state->delay_length = DELAY_MS * rate / 1000;
    state->release_holdoff = (int32_t)state->delay_length;
}

/* Takes over the set the UI task left, fading from what played when the rate
 * is the same. A set left meanwhile moves the generation again, and the next
 * call takes that one. The state starts over when the compressor turns on or
 * the rate changes, as in Rockbox; new values while it runs keep it. Turning
 * on, the old state plays alone until the new delay line is full, so the fade
 * blends into delayed samples, not into the silence the line held. A rate
 * change comes with a track, so it ends a fade instead of starting one. */
static void take(uint32_t rate)
{
    int was_on = playing.set.threshold_db != 0, same_rate = rate == rate_playing;
    if (same_rate)
        fading = playing;
    rv_handoff_take(&handoff, &playing.set, &next, sizeof playing.set);
    int on = playing.set.threshold_db != 0;
    if (on) {
        compressor_design(&playing.design, &playing.set, rate);
        if (!was_on || !same_rate)
            reset(&playing, rate);
    }
    if (same_rate)
        rv_fade_start(&fade, on && !was_on ? playing.delay_length : 0);
    else
        fade.remaining = 0;
    rate_playing = rate;
}

/* The gain the curve gives a sidechain level, S7.24: between the two entries
 * the level's top six bits pick, or up to two bits over full scale between the
 * last two. -1 for a level further out. */
static inline int32_t curve_gain(const struct compressor_design *design, int32_t level)
{
    const int32_t *gain = design->gain;
    level = magnitude(level) >> SAMPLE_SHIFT; /* 15 fractional bits */
    if (level < 1 << 15) {
        unsigned int index = (unsigned int)level >> 9;
        int32_t rest = (level & 0x1ff) << 22;
        return gain[index] - rv_fracmul(rest, gain[index] - gain[index + 1]);
    }
    if (level < 1 << 17)
        return gain[64] - rv_fracmul(((level - (1 << 15)) / 3) << 16, gain[64] - gain[65]);
    return -1;
}

/* ARM: every frame is about nine 32 by 32 to 64 bit multiplies, one smull
 * each here, a library call each in Thumb. */
static void run(void *instance, int16_t *samples, uint32_t frames)
{
    struct compressor_state *state = instance;
    if (!state->set.threshold_db)
        return;
    const struct compressor_design *design = &state->design;
    int32_t gain_now = state->release_gain, holdoff = state->release_holdoff;
    int32_t x1 = state->hp_x1, y1 = state->hp1_y1, y2 = state->hp2_y1;
    int16_t(*delay)[2] = state->delay;
    uint32_t index = state->delay_index, delay_length = state->delay_length;
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int32_t left = (int32_t)samples[0] * (1 << SAMPLE_SHIFT);
        int32_t right = (int32_t)samples[1] * (1 << SAMPLE_SHIFT);
        int32_t x = (left + right) >> 1;
        int32_t peak = magnitude(left), right_peak = magnitude(right);
        if (right_peak > peak)
            peak = right_peak;

        /* y[n] = a (y[n-1] + x[n] - x[n-1]): the two high-pass filters, then
         * x/2 + y1 + 2 y2, times 3/4. */
        y1 = scale(design->hp1, x - x1 + y1);
        y2 = scale(design->hp2, x - x1 + y2);
        x1 = x;
        int32_t level = (x >> 1) + y1 + y2 * 2;
        level >>= 1;
        level += level >> 1;
        int32_t wanted = curve_gain(design, level);

        if (wanted <= gain_now && wanted > 0) {
            gain_now = design->attack_a == UNITY
                           ? wanted
                           : scale(gain_now, design->attack_b) + scale(wanted, design->attack_a);
            /* No release before the delayed signal has passed the peak. */
            holdoff = (int32_t)delay_length;
        } else if (holdoff > 0) {
            holdoff--;
        } else {
            gain_now = scale(gain_now, design->release_b) + scale(wanted, design->release_a);
        }

        int32_t total = scale(gain_now, design->makeup);
        /* The look-ahead limiter: a peak that would clip pulls the gain down
         * before it plays. */
        if ((int32_t)(((int64_t)total * peak) >> 28) > UNITY)
            gain_now -= design->limit_a;

        int16_t delayed_left = delay[index][0], delayed_right = delay[index][1];
        delay[index][0] = samples[0];
        delay[index][1] = samples[1];
        if (++index >= delay_length)
            index = 0;
        if (total != UNITY) {
            samples[0] =
                rv_saturate16((int32_t)(((int64_t)total * delayed_left + (1 << 23)) >> 24));
            samples[1] =
                rv_saturate16((int32_t)(((int64_t)total * delayed_right + (1 << 23)) >> 24));
        } else {
            samples[0] = delayed_left;
            samples[1] = delayed_right;
        }
    }
    state->release_gain = gain_now;
    state->release_holdoff = holdoff;
    state->hp_x1 = x1;
    state->hp1_y1 = y1;
    state->hp2_y1 = y2;
    state->delay_index = index;
}

void compressor_process(int16_t *samples, uint32_t frames, uint32_t rate)
{
    if (rv_handoff_pending(&handoff) || rate != rate_playing)
        take(rate);
    rv_fade_run(&fade, run, &playing, &fading, samples, frames);
}
