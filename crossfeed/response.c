/* SPDX-License-Identifier: GPL-3.0-only */

/* What each ear hears, for the editor's plot: the Custom design of design.c
 * and the Simple loop of crossfeed.c in double precision without a math
 * library, then |H|^2 along a log frequency axis. Only the editor module
 * carries this. */

#include "shared.h"
#include "rv_math.h"

/* The precut design.c applies: Auto is the smallest half-decibel step that
 * keeps direct + cross at or below 1. */
static int precut_applied(const struct crossfeed_set *set)
{
    if (set->precut_tenths != CROSSFEED_PRECUT_AUTO)
        return set->precut_tenths;
    double sum = rv_decibels(set->direct_tenths, 200) + rv_decibels(set->cross_tenths, 200);
    if (sum <= 1.0)
        return 0;
    double half_steps = rv_log(sum) * 17.37177927613007; /* 40 / ln 10 */
    int steps = (int)half_steps;
    if (steps < half_steps)
        steps++;
    return -5 * steps;
}

/* |b0 + b1 z^-1 + b2 z^-2|^2 at the angle whose cosines are c and c2 (of twice
 * the angle). */
static double power(double b0, double b1, double b2, double c, double c2)
{
    return b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c + 2.0 * b0 * b2 * c2;
}

static int16_t to_tenths(double power_ratio)
{
    double db = rv_log(power_ratio) * 43.42944819032518;
    return (int16_t)(db + (db < 0.0 ? -0.5 : 0.5));
}

/* The Custom cross path: a first-order high shelf lifting the cross gain to
 * the attenuation, g its half gain, the cutoff divided by g to sit at the
 * -3 dB point. The precut lowers the shelf and the flat direct path. */
static void custom(const struct crossfeed_set *set, unsigned int rate, int16_t *same,
                   int16_t *other, unsigned int points, double ratio)
{
    int precut = precut_applied(set);
    double scaler = rv_decibels(set->cross_tenths + precut, 200);
    double g = rv_decibels(set->hf_tenths - set->cross_tenths, 400);
    double s, c;
    rv_sin_cos(3.141592653589793238 * set->cutoff_hz / (g * rate), &s, &c);
    double b0 = s + c * g, b1 = s - c * g, a0 = s + c / g, a1 = s - c / g;
    double hz = 20.0;
    for (unsigned int i = 0; i < points; i++) {
        rv_sin_cos(6.283185307179586477 * hz / rate, &s, &c);
        same[i] = (int16_t)(set->direct_tenths + precut);
        other[i] = to_tenths(scaler * scaler * power(b0, b1, 0.0, c, 0.0) /
                             power(a0, a1, 0.0, c, 0.0));
        hz *= ratio;
    }
}

/* Simple, from the loop in crossfeed.c: with c1 = 2128 / rate and
 * c2 = 1000 / rate, the state d = vcl - vcr follows
 * d[n+1] = (1 - c1) d[n] - 2 c2 (in[n-1] + d[n-1]) for in = L - R, and the
 * left output is L + d / 2, so the cross path is
 * H(z) = c2 z^-2 / (1 - (1 - c1) z^-1 + 2 c2 z^-2) and the own channel 1 - H. */
static void simple(unsigned int rate, int16_t *same, int16_t *other, unsigned int points,
                   double ratio)
{
    double c1 = 2128.0 / rate, c2 = 1000.0 / rate;
    double a1 = -(1.0 - c1), a2 = 2.0 * c2;
    double hz = 20.0;
    for (unsigned int i = 0; i < points; i++) {
        double s, c;
        rv_sin_cos(6.283185307179586477 * hz / rate, &s, &c);
        double c_twice = 2.0 * c * c - 1.0; /* cos 2w */
        double denominator = power(1.0, a1, a2, c, c_twice);
        other[i] = to_tenths(c2 * c2 / denominator);
        /* 1 - H = (1 + a1 z^-1 + (a2 - c2) z^-2) / (1 + a1 z^-1 + a2 z^-2). */
        same[i] = to_tenths(power(1.0, a1, a2 - c2, c, c_twice) / denominator);
        hz *= ratio;
    }
}

void crossfeed_response(const struct crossfeed_set *set, unsigned int rate, int16_t *same,
                        int16_t *other, unsigned int points)
{
    /* 20 Hz to 20 kHz is three decades; each point is one step of that. */
    double ratio = rv_exp(6.907755278982137052 / (points - 1));
    if (set->mode == CROSSFEED_CUSTOM) {
        custom(set, rate, same, other, points, ratio);
    } else if (set->mode == CROSSFEED_SIMPLE) {
        simple(rate, same, other, points, ratio);
    } else {
        for (unsigned int i = 0; i < points; i++) {
            same[i] = 0;
            other[i] = CROSSFEED_NO_PATH;
        }
    }
}
