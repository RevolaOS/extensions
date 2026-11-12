/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox lib/rbcodec/dsp/channel_mode.c.
 *
 * Copyright (C) 2006 Thom Johansen
 * Copyright (C) 2012 Michael Sevakis
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* Rockbox's channel modes on the native interleaved S16 output, in place.
 * Custom mixes each channel with the other: below 100 % a part of the other
 * channel is added, above it a part is taken away. Mono, Left Only, Right Only,
 * Karaoke and Swap need no coefficients.
 *
 * The UI task leaves a set with interrupts masked and moves the generation;
 * the pump task takes it over when it sees the generation move. Only the pump
 * task touches what plays. The coefficients are Q15 and |gain| + |cross| is at
 * most 32768, so a sample times the pair fits int32 and the output cannot
 * leave S16: Thumb multiplies suffice. */

#include "channels.h"
#include "rv_api.h"

static const struct rv_api *api;

static struct channels_set next;
static volatile uint32_t generation;

/* Pump task only. */
static struct channels_set playing;
static uint32_t taken;
static int32_t gain, cross;

void channels_start(const struct rv_api *core)
{
    api = core;
}

void channels_publish(const struct channels_set *set)
{
    uint32_t state = api->irq_save();
    next = *set;
    generation++;
    api->irq_restore(state);
}

void channels_coefficients(unsigned int percent, int32_t *gain_out, int32_t *cross_out)
{
    int32_t w = (int32_t)((percent * 32768u + 50) / 100);
    if (percent <= 100) {
        /* (1 + w) / 2 and (1 - w) / 2. */
        *gain_out = (32768 + w) >> 1;
        *cross_out = *gain_out - w;
    } else {
        /* (1 + w) / (2 w) and that less 1; the numerator pre-shifted by 14
         * stays below 2^31 up to 250 %. */
        *gain_out = (((32768 + w) << 14) + w / 2) / w;
        *cross_out = *gain_out - 32768;
    }
}

/* Takes over the set the UI task left. A set left meanwhile moves the
 * generation again, and the next call takes that one. */
static void take(void)
{
    uint32_t seen;
    do {
        seen = generation;
        playing = next;
    } while (seen != generation);
    taken = seen;
    if (playing.mode == CHANNELS_CUSTOM)
        channels_coefficients(5u * playing.width, &gain, &cross);
}

static void custom(int16_t *samples, uint32_t frames)
{
    const int32_t g = gain, c = cross;
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int32_t left = samples[0], right = samples[1];
        samples[0] = (int16_t)((left * g + right * c + 16384) >> 15);
        samples[1] = (int16_t)((right * g + left * c + 16384) >> 15);
    }
}

static void mono(int16_t *samples, uint32_t frames)
{
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int16_t mid = (int16_t)(samples[0] / 2 + samples[1] / 2);
        samples[0] = mid;
        samples[1] = mid;
    }
}

static void one_side(int16_t *samples, uint32_t frames, unsigned int heard)
{
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2)
        samples[heard ^ 1] = samples[heard];
}

static void karaoke(int16_t *samples, uint32_t frames)
{
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int16_t side = (int16_t)(samples[0] / 2 - samples[1] / 2);
        samples[0] = side;
        samples[1] = (int16_t)-side;
    }
}

static void swap(int16_t *samples, uint32_t frames)
{
    for (int16_t *end = samples + frames * 2; samples < end; samples += 2) {
        int16_t left = samples[0];
        samples[0] = samples[1];
        samples[1] = left;
    }
}

void channels_process(int16_t *samples, uint32_t frames)
{
    if (taken != generation)
        take();
    switch (playing.mode) {
    case CHANNELS_MONO:
        mono(samples, frames);
        break;
    case CHANNELS_LEFT:
        one_side(samples, frames, 0);
        break;
    case CHANNELS_RIGHT:
        one_side(samples, frames, 1);
        break;
    case CHANNELS_KARAOKE:
        karaoke(samples, frames);
        break;
    case CHANNELS_SWAP:
        swap(samples, frames);
        break;
    case CHANNELS_CUSTOM:
        custom(samples, frames);
        break;
    default:
        break;
    }
}
