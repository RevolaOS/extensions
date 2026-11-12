/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <stdint.h>

struct rv_api;

enum { RATIO_2, RATIO_4, RATIO_6, RATIO_10, RATIO_LIMIT, RATIO_COUNT };

/* What the settings say, in Rockbox's units: the threshold in whole decibels
 * below full scale (0 is off), makeup 1 for Auto, the ratio's index, knee 1
 * for Soft, and the attack and release times in milliseconds. */
struct compressor_set {
    int8_t threshold_db;
    uint8_t makeup, ratio, knee;
    uint16_t attack_ms, release_ms;
};

/* Rockbox's compression curve for a set, in S15.16 decibels: five input
 * levels and the gain offset (0 or below) at each, straight between them, and
 * the makeup gain added after. Index 0 is the bottom of the knee, 1 the
 * threshold, 2 the top of the knee, 3 full scale and 4 two bits over it. */
struct compressor_points {
    int32_t db[5], offset[5];
    int32_t makeup;
};

/* A set at one sample rate, in Rockbox's formats: the gain, S7.24, by input
 * level in steps of 1/64 of full scale (0 silence, 64 full scale, 65 two
 * bits over), the makeup gain, and the one-pole coefficients of the attack,
 * release, limiter and the two sidechain high-pass filters. */
struct compressor_design {
    int32_t gain[66];
    int32_t makeup;
    int32_t attack_a, attack_b, release_a, release_b, limit_a;
    int32_t hp1, hp2;
};

/* curve.c */
void compressor_points(const struct compressor_set *set, struct compressor_points *out);

/* design.c */
void compressor_design(struct compressor_design *out, const struct compressor_set *set,
                       unsigned int rate);

/* compressor.c: `publish` leaves a set for the pump task from any task;
 * `process` runs the set that plays on `frames` interleaved stereo S16 frames in
 * place, on the pump task only. */
void compressor_start(const struct rv_api *core);
void compressor_publish(const struct compressor_set *set);
void compressor_process(int16_t *samples, uint32_t frames, uint32_t rate);

#endif
