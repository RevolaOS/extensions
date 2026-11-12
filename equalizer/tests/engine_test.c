/* SPDX-License-Identifier: GPL-3.0-only */

/* The engine on the host, against a double-precision cascade: Apple's loader,
 * reset and process are stubs, and the ThreeBandEQ is a byte array. Includes
 * engine.c for its state. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../engine.c"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { BASS_PRESET = 8, FRAMES = 2048 };

static uint8_t record[ThreeBandEQ_bytes + 16]; /* the last 16 bytes a guard */
static unsigned int loads, resets, passes, irq_depth;

static uint32_t irq_save(void)
{
    assert(!irq_depth++);
    return 0x13;
}

static void irq_restore(uint32_t state)
{
    assert(state == 0x13 && irq_depth-- == 1);
}

static const struct rv_api fake_api = {.irq_save = irq_save, .irq_restore = irq_restore};

static uint8_t *stage(void *eq, unsigned int i)
{
    return (uint8_t *)eq + ThreeBandEQ_stages + i * EQStage_bytes;
}

/* Apple's loader: three identity stages, bypassed but for the Bass preset's
 * second, a +6 dB low shelf at 1 kHz; enabled for a preset at 44.1 or 48 kHz. */
void EQ_LoadPresetCoefficients(struct ThreeBandEQ *eq)
{
    uint32_t preset = RV_FIELD(eq, ThreeBandEQ_preset, uint32_t);
    uint32_t rate = RV_FIELD(eq, ThreeBandEQ_sample_rate, uint32_t);
    assert(preset != EQ_CUSTOM_PRESET);
    loads++;
    for (unsigned int i = 0; i < NATIVE_STAGES; i++) {
        int32_t shelf[5] = {UNITY, 0, 0, 0, 0};
        int bass = i == 1 && preset == BASS_PRESET && supported(rate);
        if (bass)
            assert(eq_biquad(shelf, EQ_LOW_SHELF, 1000, 7, 60, 0, rate));
        RV_FIELD(stage(eq, i), EQStage_b0, int32_t) = shelf[0];
        RV_FIELD(stage(eq, i), EQStage_b1, int32_t) = shelf[1];
        RV_FIELD(stage(eq, i), EQStage_b2, int32_t) = shelf[2];
        RV_FIELD(stage(eq, i), EQStage_a1, int32_t) = shelf[3];
        RV_FIELD(stage(eq, i), EQStage_a2, int32_t) = shelf[4];
        RV_FIELD(stage(eq, i), EQStage_bypass, uint8_t) = !bass;
    }
    RV_FIELD(eq, ThreeBandEQ_enabled, uint8_t) = preset && supported(rate);
}

void EQ_ResetAllBiquadStates(struct ThreeBandEQ *eq)
{
    (void)eq;
    resets++;
}

void EQ_ProcessThreeBiquadsInPlace(struct ThreeBandEQ *eq, int16_t *input, uint32_t frames,
                                   int16_t **output, uint32_t *output_frames)
{
    (void)eq;
    passes++;
    *output = input;
    *output_frames = frames;
}

static void process(int16_t *samples, unsigned int frames)
{
    int16_t *output = NULL;
    uint32_t count = 0;
    eq_process((struct ThreeBandEQ *)record, samples, frames, &output, NULL, &count);
    assert(output == samples && count == frames);
}

static void load(uint32_t preset, uint32_t rate)
{
    RV_FIELD(record, ThreeBandEQ_preset, uint32_t) = preset;
    RV_FIELD(record, ThreeBandEQ_sample_rate, uint32_t) = rate;
    eq_load((struct ThreeBandEQ *)record, NULL);
}

static void noise(int16_t *samples, unsigned int frames)
{
    for (unsigned int i = 0; i < frames; i++) {
        samples[i * 2] = (int16_t)((int)(i * 71 % 2000) - 1000);
        samples[i * 2 + 1] = (int16_t)((int)(i * 173 % 4000) - 2000);
    }
}

static void sine(int16_t *samples, unsigned int frames, double hz, double amplitude)
{
    for (unsigned int i = 0; i < frames; i++)
        samples[i * 2] = samples[i * 2 + 1] = (int16_t)lrint(amplitude * sin(2 * M_PI * hz * i / 44100));
}

/* A double-precision cascade from rest; returns the largest magnitude before
 * saturation. */
static double reference(const int32_t (*k)[5], unsigned int stages, const int16_t *in,
                        int16_t *out, unsigned int frames)
{
    double peak = 0;
    for (unsigned int c = 0; c < 2; c++) {
        double h[EQ_BANDS][4] = {{0}};
        for (unsigned int i = 0; i < frames; i++) {
            double x = in[i * 2 + c];
            for (unsigned int s = 0; s < stages; s++) {
                double y = (k[s][0] * x + k[s][1] * h[s][0] + k[s][2] * h[s][1] - k[s][3] * h[s][2] -
                            k[s][4] * h[s][3]) /
                           16777216.0;
                h[s][1] = h[s][0];
                h[s][0] = x;
                h[s][3] = h[s][2];
                h[s][2] = y;
                x = y;
            }
            peak = fmax(peak, fabs(x));
            long rounded = lrint(x);
            out[i * 2 + c] = (int16_t)(rounded > 32767 ? 32767 : rounded < -32768 ? -32768 : rounded);
        }
    }
    return peak;
}

static int max_difference(const int16_t *a, const int16_t *b, unsigned int frames)
{
    int worst = 0;
    for (unsigned int i = 0; i < frames * 2; i++) {
        int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        worst = d > worst ? d : worst;
    }
    return worst;
}

/* Choices: Auto or a precut in tenths below zero; the frequency as an index. */
static unsigned int precut_choice(int tenths)
{
    return (unsigned int)(1 - tenths);
}
static unsigned int frequency_choice(unsigned int hz)
{
    for (unsigned int i = 0;; i++)
        if (eq_frequencies[i] == hz)
            return i;
}
static unsigned int gain_choice(int tenths)
{
    return (unsigned int)(tenths + 120) / 5;
}
static unsigned int q_choice(int tenths)
{
    return (unsigned int)(tenths - 3);
}

static void band(uint8_t *values, unsigned int i, unsigned int type, unsigned int hz, int q_tenths,
                 int gain_tenths)
{
    values[eq_slot(i, EQ_TYPE)] = (uint8_t)type;
    values[eq_slot(i, EQ_FREQUENCY)] = (uint8_t)frequency_choice(hz);
    values[eq_slot(i, EQ_Q)] = (uint8_t)q_choice(q_tenths);
    values[eq_slot(i, EQ_GAIN)] = (uint8_t)gain_choice(gain_tenths);
}

static void flat(uint8_t *values, unsigned int precut)
{
    memset(values, 0, EQ_VALUES);
    values[EQ_PRECUT] = (uint8_t)precut;
    for (unsigned int i = 0; i < EQ_BANDS; i++)
        band(values, i, EQ_OFF, 1000, 7, 0);
}

static void histories_at_rest(void)
{
    struct eq_stage rest;
    eq_cascade_rest(&rest, 1);
    for (unsigned int i = 0; i < EQ_BANDS; i++)
        for (unsigned int c = 0; c < 2; c++) {
            for (unsigned int n = 0; n < 4; n++)
                assert(!playing[i].history[c][n]);
            assert(playing[i].error[c] == rest.error[0]);
        }
}

int main(void)
{
    static int16_t samples[FRAMES * 2], expected[FRAMES * 2], previous[FRAMES * 2];
    static struct eq_stage old_stages[EQ_BANDS], new_stages[EQ_BANDS];
    uint8_t values[EQ_VALUES], untouched[sizeof record];
    eq_engine_start(&fake_api);
    memset(record + ThreeBandEQ_bytes, 0xdb, 16);

    /* Before the first load, Apple's process runs. */
    noise(samples, 64);
    process(samples, 64);
    assert(passes == 1 && !owner);

    for (unsigned int rate = 44100; rate <= 48000; rate += 3900) {
        unsigned int bank = rate == 48000;
        /* A manual 6.5 dB precut and three bells match the double cascade. */
        flat(values, precut_choice(-65));
        band(values, 0, EQ_PEAKING, 60, 7, 50);
        band(values, 1, EQ_PEAKING, 160, 7, 50);
        band(values, 2, EQ_PEAKING, 1000, 7, 50);
        assert(eq_publish(values) && custom_precut[bank] == 65);
        load(EQ_CUSTOM_PRESET, rate);
        assert(RV_FIELD(record, ThreeBandEQ_enabled, uint8_t) && owner == (void *)record);
        memcpy(untouched, record, sizeof record);
        noise(samples, 256);
        reference(custom[bank], EQ_BANDS, samples, expected, 256);
        process(samples, 256);
        assert(max_difference(samples, expected, 256) <= 1);
        assert(!memcmp(record, untouched, sizeof record) && passes == 1);
        assert(playing[3].bypass && playing[4].bypass);

        /* A live edit fades from the old five stages to the new over 128 frames. */
        memcpy(old_stages, playing, sizeof old_stages);
        band(values, 3, EQ_PEAKING, 4000, 7, 50);
        band(values, 4, EQ_PEAKING, 8000, 7, 50);
        assert(eq_publish(values) && next.fade);
        for (unsigned int i = 0; i < EQ_BANDS; i++) {
            new_stages[i].b0 = custom[bank][i][0];
            new_stages[i].b1 = custom[bank][i][1];
            new_stages[i].b2 = custom[bank][i][2];
            new_stages[i].a1 = -custom[bank][i][3];
            new_stages[i].a2 = -custom[bank][i][4];
            new_stages[i].bypass = 0;
        }
        eq_cascade_rest(new_stages, EQ_BANDS);
        noise(samples, 256);
        memcpy(expected, samples, 512 * sizeof *samples);
        memcpy(previous, samples, 512 * sizeof *samples);
        eq_cascade(old_stages, EQ_BANDS, previous, 128);
        eq_cascade(new_stages, EQ_BANDS, expected, 256);
        for (unsigned int i = 0; i < 128; i++)
            for (unsigned int c = 0; c < 2; c++) {
                unsigned int n = i * 2 + c;
                expected[n] = (int16_t)(((int32_t)previous[n] * (127 - (int)i) +
                                         (int32_t)expected[n] * ((int)i + 1)) >>
                                        7);
            }
        process(samples, 17);
        assert(fade.remaining == 111);
        process(samples + 34, 239);
        assert(!fade.remaining && !memcmp(samples, expected, 512 * sizeof *samples));
        assert(!memcmp(record, untouched, sizeof record));

        /* A reset puts the histories at rest and ends a fade. */
        values[eq_slot(4, EQ_GAIN)] = (uint8_t)gain_choice(-50);
        assert(eq_publish(values));
        process(samples, 1);
        assert(fade.remaining == 127);
        eq_reset((struct ThreeBandEQ *)record, NULL);
        assert(rest_pending);
        process(samples, 1);
        assert(!fade.remaining);
        eq_reset((struct ThreeBandEQ *)record, NULL);
        process(samples, 0);
        histories_at_rest();
    }

    /* A +12 dB bell and its inverse are transparent at a 30000 peak; between
     * them the signal passes 60000, which a 16-bit stage would saturate. */
    flat(values, precut_choice(0));
    band(values, 0, EQ_PEAKING, 1000, 7, 120);
    band(values, 1, EQ_PEAKING, 1000, 7, -120);
    assert(eq_publish(values) && custom_precut[0] == 0);
    load(EQ_CUSTOM_PRESET, 44100);
    sine(samples, FRAMES, 1000, 30000);
    assert(reference(custom[0], 1, samples, expected, FRAMES) > 60000);
    memcpy(previous, samples, sizeof samples);
    process(samples, FRAMES);
    assert(max_difference(samples, previous, FRAMES) <= 2);

    /* Auto precut takes a +6 dB low shelf's boost back out; 0 dB would clip. */
    flat(values, precut_choice(0));
    band(values, 0, EQ_LOW_SHELF, 1000, 7, 60);
    assert(eq_publish(values));
    sine(samples, FRAMES, 100, 32767);
    assert(reference(custom[0], EQ_BANDS, samples, expected, FRAMES) > 60000);
    values[EQ_PRECUT] = EQ_PRECUT_AUTO;
    assert(eq_publish(values));
    assert(custom_precut[0] >= 60 && custom_precut[0] <= 61 && eq_peak(custom[0], 5, 44100) <= 0);
    load(EQ_CUSTOM_PRESET, 44100);
    assert(reference(custom[0], EQ_BANDS, samples, expected, FRAMES) <= 32767.5);
    process(samples, FRAMES);
    assert(max_difference(samples, expected, FRAMES) <= 1);

    /* Error feedback keeps low poles from amplifying the rounding: a 20 Hz bell
     * and a 60 Hz shelf, both +12 dB, track the double cascade. */
    flat(values, precut_choice(0));
    band(values, 0, EQ_PEAKING, 20, 19, 120);
    band(values, 1, EQ_LOW_SHELF, 60, 7, 120);
    assert(eq_publish(values));
    load(EQ_CUSTOM_PRESET, 44100);
    sine(samples, FRAMES, 1000, 1000);
    reference(custom[0], EQ_BANDS, samples, expected, FRAMES);
    process(samples, FRAMES);
    double error = 0;
    for (unsigned int i = 0; i < FRAMES * 2; i++)
        error += (double)(samples[i] - expected[i]) * (samples[i] - expected[i]);
    assert(sqrt(error / (FRAMES * 2)) < 0.2);

    /* An Apple preset: its stages are copied, the first cut by the peak, and
     * its record is left as Apple's loader wrote it. */
    unsigned int before = loads;
    load(BASS_PRESET, 44100);
    assert(loads == before + 1 && RV_FIELD(record, ThreeBandEQ_enabled, uint8_t));
    assert(!plays_custom && next.count == 1);
    int32_t shelf[5], attenuated[5];
    assert(eq_biquad(shelf, EQ_LOW_SHELF, 1000, 7, 60, 0, 44100));
    assert(RV_FIELD(stage(record, 1), EQStage_b0, int32_t) == shelf[0]);
    int peak = eq_peak((const int32_t (*)[5])shelf, 1, 44100);
    assert(peak >= 60 && peak <= 61 && (native_precut & 0xff) == (uint32_t)peak);
    memcpy(attenuated, shelf, sizeof shelf);
    eq_attenuate(attenuated, (unsigned int)peak);
    assert(!memcmp(next.coefficients[0], attenuated, sizeof attenuated));
    uint32_t cached = native_precut;
    load(BASS_PRESET, 44100);
    assert(loads == before + 2 && native_precut == cached);
    eq_reset((struct ThreeBandEQ *)record, NULL);
    sine(samples, FRAMES, 100, 32767);
    assert(reference((const int32_t (*)[5])attenuated, 1, samples, expected, FRAMES) <= 32767.5);
    process(samples, FRAMES);
    assert(max_difference(samples, expected, FRAMES) <= 1);

    /* An unsupported rate: nothing plays, for Apple's presets or Custom. */
    load(BASS_PRESET, 32000);
    assert(!RV_FIELD(record, ThreeBandEQ_enabled, uint8_t) && !next.count);
    load(EQ_CUSTOM_PRESET, 32000);
    assert(!RV_FIELD(record, ThreeBandEQ_enabled, uint8_t) && !next.count && !plays_custom);
    noise(samples, 256);
    memcpy(expected, samples, 512 * sizeof *samples);
    process(samples, 256);
    assert(!memcmp(samples, expected, 512 * sizeof *samples));

    /* Off: Apple's loader disables the EQ; the engine has nothing to play. */
    load(0, 44100);
    assert(!RV_FIELD(record, ThreeBandEQ_enabled, uint8_t) && !next.count);

    /* Stopping while Custom plays turns the EQ off in Apple's record. */
    load(EQ_CUSTOM_PRESET, 44100);
    assert(RV_FIELD(record, ThreeBandEQ_enabled, uint8_t));
    eq_engine_stop();
    assert(!RV_FIELD(record, ThreeBandEQ_enabled, uint8_t) && !RV_FIELD(record, ThreeBandEQ_preset, uint32_t));
    for (unsigned int i = 0; i < 16; i++)
        assert(record[ThreeBandEQ_bytes + i] == 0xdb);
    assert(passes == 1 && resets == 5);
    puts("engine: cascade headroom, precut, crossfade, reset and stop passed");
    return 0;
}
