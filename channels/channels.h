/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CHANNELS_H
#define CHANNELS_H

#include <stdint.h>

struct rv_api;

enum {
    CHANNELS_STEREO,
    CHANNELS_MONO,
    CHANNELS_LEFT,
    CHANNELS_RIGHT,
    CHANNELS_KARAOKE,
    CHANNELS_SWAP,
    CHANNELS_CUSTOM,
    CHANNELS_MODES,
};

/* What the settings say: the mode, and the Custom width as an index, 5 % per
 * step from 0 to 250 %. */
struct channels_set {
    uint8_t mode;
    uint8_t width;
};

/* Rockbox's width as Q15: `gain` for a channel's own sample, `cross` for the
 * other's. |gain| + |cross| never exceeds 32768. */
void channels_coefficients(unsigned int percent, int32_t *gain, int32_t *cross);

/* `publish` leaves a set for the pump task from any task; `process` runs the
 * set that plays on `frames` interleaved stereo S16 frames in place, on the pump
 * task only. */
void channels_start(const struct rv_api *core);
void channels_publish(const struct channels_set *set);
void channels_process(int16_t *samples, uint32_t frames);

#endif
