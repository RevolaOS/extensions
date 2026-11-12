/* SPDX-License-Identifier: GPL-3.0-only */

/* The Custom cross path from the settings, as Rockbox designs it: a bilinear
 * first-order high shelf whose cutoff is moved to the -3 dB point, scaled by
 * the cross gain. Fixed point throughout: gains are read from a quarter-decibel
 * table, the angle's sine and cosine come from a short series, and the one
 * division is a reciprocal by shift and subtract. */

#include "crossfeed.h"

enum {
    GAIN_SHIFT = 28,   /* s3.28: the table, the shelf terms */
    TRIG_SHIFT = 30,   /* s1.30: sine and cosine, so 1.0 has room */
    TABLE_FIRST = -48, /* quarter decibels of the first entry, -12 dB */
    TABLE_LAST = 42,   /* +10.5 dB */
    QUARTER_PI_Q29 = 421657428,
    /* 0.45 pi / 4 in Q31: the shelf's cutoff stays below 0.45 of the sample rate. */
    QUARTER_LIMIT_Q31 = 758983371,
};

/* 10 to the power k / 80 for k from TABLE_FIRST to TABLE_LAST, in s3.28: the
 * cross gain, direct gain and the shelf's half gain and its inverse all lie in
 * this range in steps of a quarter decibel. */
static const uint32_t gains[TABLE_LAST - TABLE_FIRST + 1] = {
    67427938, 69396869, 71423294, 73508892, 75655391, 77864568, 80138254, 82478334, 84886745,
    87365482, 89916600, 92542213, 95244494, 98025683, 100888085, 103834070, 106866080,
    109986626, 113198293, 116503743, 119905714, 123407024, 127010574, 130719350, 134536424,
    138464958, 142508208, 146669523, 150952350, 155360238, 159896839, 164565911, 169371322,
    174317054, 179407204, 184645989, 190037749, 195586952, 201298194, 207176207, 213225862,
    219452169, 225860289, 232455528, 239243352, 246229384, 253419412, 260819392, 268435456,
    276273913, 284341257, 292644171, 301189535, 309984429, 319036137, 328352161, 337940217,
    347808249, 357964434, 368417184, 379175160, 390247274, 401642701, 413370879, 425441527,
    437864644, 450650522, 463809755, 477353244, 491292211, 505638202, 520403104, 535599149,
    551238927, 567335394, 583901887, 600952130, 618500249, 636560782, 655148692, 674279380,
    693968693, 714232945, 735088923, 756553907, 778645680, 801382545, 824783337, 848867446,
    873654823, 899166004,
};

static uint32_t gain_quarter_db(int quarters)
{
    return gains[quarters - TABLE_FIRST];
}

static inline int64_t mul(int32_t a, int32_t b)
{
    return (int64_t)a * b;
}

/* 2^57 / divisor, for a divisor in s3.28 between 0.25 and 4: s2.29. */
static uint32_t reciprocal(uint32_t divisor)
{
    uint64_t remainder = (uint64_t)1 << 57;
    uint32_t quotient = 0;
    for (int bit = 31; bit >= 0; bit--) {
        uint64_t term = (uint64_t)divisor << bit;
        if (remainder >= term) {
            remainder -= term;
            quotient |= 1u << bit;
        }
    }
    return quotient;
}

/* Sine and cosine of four times x, x in Q31 below 0.7 rad, as s1.30: the
 * series to the thirteenth power for x, then the angle doubled twice. */
static void sine_cosine(uint32_t x, int32_t *sine, int32_t *cosine)
{
    int32_t x2 = (int32_t)(mul((int32_t)x, (int32_t)x) >> 31); /* Q31 */
    int32_t s = (int32_t)(x >> 1), term = s;                   /* Q30 */
    int32_t c = 1 << TRIG_SHIFT, cterm = c;
    for (int n = 1; n <= 6; n++) {
        cterm = (int32_t)(-mul(cterm, x2) >> 31) / ((2 * n - 1) * (2 * n));
        term = (int32_t)(-mul(term, x2) >> 31) / ((2 * n) * (2 * n + 1));
        c += cterm;
        s += term;
    }
    for (int twice = 0; twice < 2; twice++) {
        int32_t s2 = (int32_t)(mul(s, c) >> (TRIG_SHIFT - 1));
        c = (int32_t)((mul(c, c) - mul(s, s)) >> TRIG_SHIFT);
        s = s2;
    }
    *sine = s;
    *cosine = c;
}

void crossfeed_design(struct crossfeed_custom *out, unsigned int rate, int direct_tenths,
                      int cross_tenths, int hf_tenths, unsigned int cutoff_hz)
{
    /* All gains come in steps of half a decibel, so the quarter-decibel table
     * holds every gain and every half gain used here. */
    uint32_t scaler = gain_quarter_db(cross_tenths * 2 / 5);
    uint32_t direct = gain_quarter_db(direct_tenths * 2 / 5);
    /* The shelf lifts the cross gain to the attenuation at high frequencies;
     * g is the square root of that gain. The cutoff is divided by g to sit at
     * the -3 dB point. */
    int half = (hf_tenths - cross_tenths) / 5;
    uint32_t g = gain_quarter_db(half), inverse_g = gain_quarter_db(-half);

    /* A quarter of the half angle, pi f / (4 g rate). The deepest shelf cut
     * moves a 2 kHz cutoff to 6.7 kHz, above Nyquist at the low rates, where
     * the bilinear shelf turns unstable; held at 0.45 of the rate the
     * denominator stays positive. */
    uint32_t quarter =
        (uint32_t)(mul((int32_t)crossfeed_ratio_q31(cutoff_hz, rate), QUARTER_PI_Q29) >> 29);
    quarter = (uint32_t)(mul((int32_t)quarter, (int32_t)inverse_g) >> GAIN_SHIFT);
    if (quarter > QUARTER_LIMIT_Q31)
        quarter = QUARTER_LIMIT_Q31;
    int32_t sine, cosine;
    sine_cosine(quarter, &sine, &cosine);

    /* s3.28 shelf terms: b0 = s + c g, b1 = s - c g, a0 = s + c / g, a1 = s - c / g. */
    int32_t s = sine >> (TRIG_SHIFT - GAIN_SHIFT);
    int32_t cg = (int32_t)(mul(cosine, (int32_t)g) >> TRIG_SHIFT);
    int32_t c_over_g = (int32_t)(mul(cosine, (int32_t)inverse_g) >> TRIG_SHIFT);
    int32_t b0 = s + cg, b1 = s - cg, a0 = s + c_over_g, a1 = s - c_over_g;

    /* The cross gain scales the numerator before the division, which keeps
     * every product below full scale. */
    b0 = (int32_t)(mul(b0, (int32_t)scaler) >> GAIN_SHIFT);
    b1 = (int32_t)(mul(b1, (int32_t)scaler) >> GAIN_SHIFT);
    int32_t rcp = (int32_t)reciprocal((uint32_t)a0);
    out->coefs[0] = (int32_t)(mul(b0, rcp) >> 26);
    out->coefs[1] = (int32_t)(mul(b1, rcp) >> 26);
    out->coefs[2] = (int32_t)(-mul(a1, rcp) >> 26);
    out->gain = direct >= (1u << GAIN_SHIFT) ? 0x7fffffff : (int32_t)(direct << 3);
}
