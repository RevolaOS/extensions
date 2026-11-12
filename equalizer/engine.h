/* SPDX-License-Identifier: GPL-3.0-only */

/* The audio path of the main module. */

#ifndef EQUALIZER_ENGINE_H
#define EQUALIZER_ENGINE_H
#include "shared.h"

struct rv_api;
struct ThreeBandEQ;

/* One biquad of the cascade: coefficients with 24 fraction bits, a1 and a2
 * negated, and per channel x1, x2, y1, y2 and the error fed into the next
 * output. */
struct eq_stage {
    int32_t b0, b1, b2, a1, a2;
    int32_t history[2][4];
    int32_t error[2];
    uint32_t bypass;
};

/* Runs the stages that are not bypassed over interleaved stereo, in place. */
void eq_cascade(struct eq_stage *stages, unsigned int count, int16_t *samples, uint32_t frames);
/* Histories at rest. */
void eq_cascade_rest(struct eq_stage *stages, unsigned int count);

/* The Custom preset is the DSP preset and the EQ menu row after Apple's last. */
#define EQ_CUSTOM_PRESET EQ_PresetCount

void eq_engine_start(const struct rv_api *core);
/* Before the hooks are unbound: turns the EQ off if it plays the Custom preset,
 * which Apple's own code cannot. */
void eq_engine_stop(void);

/* Makes `values` the Custom preset, crossfading when it plays. 0, and nothing
 * changes, when a band has no stable filter. UI task. */
int eq_publish(const uint8_t values[EQ_VALUES]);
/* struct eq_link's response, of the Custom preset. UI task. */
unsigned int eq_custom_response(int16_t *tenths, unsigned int points);

/* Hook handlers: load and reset on the task that changed the setting or
 * prepared a track, process on the PCM output pump task. */
void eq_load(struct ThreeBandEQ *eq, const void *next_hook);
void eq_reset(struct ThreeBandEQ *eq, const void *next_hook);
void eq_process(struct ThreeBandEQ *eq, int16_t *samples, uint32_t frames, int16_t **output,
                const void *next_hook, uint32_t *output_frames);

#endif
