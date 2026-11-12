/* SPDX-License-Identifier: GPL-3.0-only */

/* What the EQ plays. Apple's loader still fills its three stages for its own
 * presets; the engine copies them, cuts the first by the cascade's peak so a
 * boost has room, and runs them through its own cascade. The Custom preset is
 * five stages Apple's loader never sees. Apple's records keep what Apple wrote.
 *
 * Load and reset arrive on the task that changed the setting or prepared a
 * track, process on the PCM output pump task. Only the pump task touches the
 * stages that play. The others leave it a set of coefficients with interrupts
 * masked, and it takes the set over once it sees the generation move.
 *
 * Adapted from RepriseOS payload/eq_audio.c (93f6a45) by lgreitis. */

#include "engine.h"
#include "biquad.h"
#include "osos-2.0.5.h"
#include "rv_audio.h"
#include "rv_libc.h"

enum {
    NATIVE_STAGES = 3,
    FADE_FRAMES = 128,
    FADE_CHUNK_FRAMES = 32,
    PRECUT_LIMIT = 100,
    UNITY = 1 << 24,
};

static const struct rv_api *api;

/* The Custom preset at 44.1 and 48 kHz, each stage b0, b1, b2, a1, a2, and the
 * precut inside its first stage. */
static int32_t custom[2][EQ_BANDS][5];
static uint8_t custom_precut[2];

/* The set left for the pump task, and what the last load found. */
struct eq_set {
    int32_t coefficients[EQ_BANDS][5];
    uint8_t count;
    uint8_t fade; /* blend from what plays instead of switching */
};
static struct eq_set next;
static struct rv_handoff handoff;
static struct ThreeBandEQ *volatile owner; /* NULL until the first load */
static uint8_t plays_custom, custom_bank;
static volatile uint8_t rest_pending;

/* The precut of the last Apple preset loaded: tenths of a decibel, the preset
 * from bit 8 and the rate's bank in bit 16. Preset 0 has no stages, so 0 is
 * no entry. */
static uint32_t native_precut;

/* Pump task only. */
static struct eq_stage playing[EQ_BANDS], fading[EQ_BANDS];
static uint32_t fade_remaining;

void eq_engine_start(const struct rv_api *core)
{
    api = core;
}

static int supported(unsigned int rate)
{
    return rate == 44100 || rate == 48000;
}

static unsigned int precut_for(const int32_t (*set)[5], unsigned int count, unsigned int rate)
{
    int peak = eq_peak(set, count, rate);
    return peak <= 0 ? 0 : peak > PRECUT_LIMIT ? PRECUT_LIMIT : (unsigned int)peak;
}

/* Interrupts masked. */
static void leave(const int32_t (*set)[5], unsigned int count, unsigned int fade)
{
    for (unsigned int i = 0; i < count; i++)
        for (unsigned int c = 0; c < 5; c++)
            next.coefficients[i][c] = set[i][c];
    next.count = (uint8_t)count;
    next.fade = (uint8_t)fade;
    handoff.generation++;
}

int eq_publish(const uint8_t values[EQ_VALUES])
{
    int32_t banks[2][EQ_BANDS][5];
    unsigned int precut[2];
    for (unsigned int bank = 0; bank < 2; bank++) {
        unsigned int rate = bank ? 48000 : 44100;
        int automatic = values[EQ_PRECUT] == EQ_PRECUT_AUTO;
        precut[bank] = automatic ? 0 : (unsigned int)-eq_precut_tenths(values[EQ_PRECUT]);
        for (unsigned int band = 0; band < EQ_BANDS; band++) {
            const uint8_t *v = values + band * (EQ_PARAMETERS - 1);
            if (!eq_biquad(banks[bank][band], v[EQ_TYPE], eq_frequencies[v[EQ_FREQUENCY]],
                           (unsigned int)eq_q_tenths(v[EQ_Q]), eq_gain_tenths(v[EQ_GAIN]),
                           band ? 0 : precut[bank], rate))
                return 0;
        }
        if (automatic) {
            precut[bank] = precut_for(banks[bank], EQ_BANDS, rate);
            eq_attenuate(banks[bank][0], precut[bank]);
        }
    }
    uint32_t state = api->irq_save();
    memcpy(custom, banks, sizeof custom);
    custom_precut[0] = (uint8_t)precut[0];
    custom_precut[1] = (uint8_t)precut[1];
    if (plays_custom)
        leave(custom[custom_bank], EQ_BANDS, 1);
    api->irq_restore(state);
    return 1;
}

unsigned int eq_custom_response(int16_t *tenths, unsigned int points)
{
    unsigned int bank = custom_bank;
    eq_response(custom[bank], bank ? 48000 : 44100, tenths, points);
    return custom_precut[bank];
}

/* Works out what `eq` plays after a load and leaves it for the pump task. */
static void prepare(struct ThreeBandEQ *eq)
{
    int32_t set[NATIVE_STAGES][5];
    unsigned int count = 0, rate = RV_FIELD(eq, ThreeBandEQ_sample_rate, uint32_t);
    unsigned int preset = RV_FIELD(eq, ThreeBandEQ_preset, uint32_t), bank = rate == 48000;
    if (preset == EQ_CUSTOM_PRESET) {
        RV_FIELD(eq, ThreeBandEQ_enabled, uint8_t) = (uint8_t)supported(rate);
        count = supported(rate) ? EQ_BANDS : 0;
    } else if (RV_FIELD(eq, ThreeBandEQ_enabled, uint8_t)) {
        for (unsigned int i = 0; i < NATIVE_STAGES; i++) {
            uint8_t *stage = &RV_FIELD(eq, ThreeBandEQ_stages + i * EQStage_bytes, uint8_t);
            if (RV_FIELD(stage, EQStage_bypass, uint8_t))
                continue;
            set[count][0] = RV_FIELD(stage, EQStage_b0, int32_t);
            set[count][1] = RV_FIELD(stage, EQStage_b1, int32_t);
            set[count][2] = RV_FIELD(stage, EQStage_b2, int32_t);
            set[count][3] = RV_FIELD(stage, EQStage_a1, int32_t);
            set[count][4] = RV_FIELD(stage, EQStage_a2, int32_t);
            count++;
        }
        uint32_t key = preset << 8 | bank << 16, cached = native_precut;
        if (count && (cached & ~0xffu) != key) {
            cached = key | precut_for(set, count, rate);
            native_precut = cached;
        }
        if (count)
            eq_attenuate(set[0], cached & 0xff);
    }
    uint32_t state = api->irq_save();
    owner = eq;
    plays_custom = preset == EQ_CUSTOM_PRESET && count;
    custom_bank = (uint8_t)bank;
    leave(plays_custom ? custom[bank] : set, count, 0);
    api->irq_restore(state);
}

void eq_load(struct ThreeBandEQ *eq, const void *next_hook)
{
    (void)next_hook;
    /* Apple's loader has no row for the Custom preset. */
    if (RV_FIELD(eq, ThreeBandEQ_preset, uint32_t) != EQ_CUSTOM_PRESET)
        EQ_LoadPresetCoefficients(eq);
    prepare(eq);
}

void eq_reset(struct ThreeBandEQ *eq, const void *next_hook)
{
    (void)next_hook;
    EQ_ResetAllBiquadStates(eq);
    rest_pending = 1;
}

/* Takes over the set a load or publish left. A set left meanwhile moves the
 * generation again, and the next call takes that one. */
static void take(void)
{
    struct eq_set set;
    memcpy(fading, playing, sizeof fading);
    rv_handoff_take(&handoff, &set, &next, sizeof set);
    for (unsigned int i = 0; i < EQ_BANDS; i++) {
        const int32_t *c = set.coefficients[i];
        struct eq_stage *stage = &playing[i];
        stage->b0 = c[0];
        stage->b1 = c[1];
        stage->b2 = c[2];
        stage->a1 = -c[3];
        stage->a2 = -c[4];
        stage->bypass =
            i >= set.count || (c[0] == UNITY && !(c[1] | c[2] | c[3] | c[4]));
    }
    eq_cascade_rest(playing, EQ_BANDS);
    fade_remaining = set.fade ? FADE_FRAMES : 0;
}

void eq_process(struct ThreeBandEQ *eq, int16_t *samples, uint32_t frames, int16_t **output,
                const void *next_hook, uint32_t *output_frames)
{
    (void)next_hook;
    /* Bound while a preset plays: Apple's stages as they are until the next load. */
    if (owner != eq) {
        EQ_ProcessThreeBiquadsInPlace(eq, samples, frames, output, output_frames);
        return;
    }
    *output = samples;
    *output_frames = frames;
    if (rv_handoff_pending(&handoff))
        take();
    if (rest_pending) {
        rest_pending = 0;
        eq_cascade_rest(playing, EQ_BANDS);
        fade_remaining = 0;
    }
    while (fade_remaining && frames) {
        int16_t previous[FADE_CHUNK_FRAMES * 2];
        unsigned int chunk = frames < FADE_CHUNK_FRAMES ? frames : FADE_CHUNK_FRAMES;
        if (chunk > fade_remaining)
            chunk = fade_remaining;
        memcpy(previous, samples, chunk * 2 * sizeof *samples);
        eq_cascade(fading, EQ_BANDS, previous, chunk);
        eq_cascade(playing, EQ_BANDS, samples, chunk);
        for (unsigned int i = 0; i < chunk * 2; i += 2) {
            int32_t weight = (int32_t)(FADE_FRAMES + 1 - fade_remaining);
            for (unsigned int n = i; n < i + 2; n++)
                samples[n] = (int16_t)((previous[n] * (FADE_FRAMES - weight) + samples[n] * weight) /
                                       FADE_FRAMES);
            fade_remaining--;
        }
        frames -= chunk;
        samples += chunk * 2;
    }
    eq_cascade(playing, EQ_BANDS, samples, frames);
}

void eq_engine_stop(void)
{
    struct ThreeBandEQ *eq = owner;
    uint32_t state = api->irq_save();
    if (eq && RV_FIELD(eq, ThreeBandEQ_preset, uint32_t) == EQ_CUSTOM_PRESET) {
        RV_FIELD(eq, ThreeBandEQ_enabled, uint8_t) = 0;
        RV_FIELD(eq, ThreeBandEQ_preset, uint32_t) = 0;
    }
    api->irq_restore(state);
}
