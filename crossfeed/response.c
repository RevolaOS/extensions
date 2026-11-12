/* SPDX-License-Identifier: GPL-3.0-only */

/* The Custom cross path's response for the editor's plot: the design of
 * design.c in double precision without a math library, then |H|^2 along a
 * log frequency axis, lowered by the precut. Only the editor module carries
 * this. */

#include "shared.h"

/* Callers keep |x| below 1.5: decibel gains and the axis step. */
static double exponential(double x)
{
    double sum = 1.0, term = 1.0;
    for (unsigned int n = 1; n <= 18; n++) {
        term *= x / n;
        sum += term;
    }
    return sum;
}

/* Frequencies stay below Nyquist, so 0 <= x < pi. */
static void sine_cosine(double x, double *sine, double *cosine)
{
    double s = x, c = 1.0, st = x, ct = 1.0;
    for (unsigned int n = 1; n <= 11; n++) {
        st *= -x * x / ((2 * n) * (2 * n + 1));
        ct *= -x * x / ((2 * n - 1) * (2 * n));
        s += st;
        c += ct;
    }
    *sine = s;
    *cosine = c;
}

/* Natural log by range reduction and the atanh series. */
static double logarithm(double value)
{
    int power = 0;
    while (value >= 2.0) {
        value *= 0.5;
        power++;
    }
    while (value < 1.0) {
        value *= 2.0;
        power--;
    }
    double z = (value - 1.0) / (value + 1.0), term = z, sum = z;
    for (unsigned int n = 3; n <= 23; n += 2) {
        term *= z * z;
        sum += term / n;
    }
    return 2.0 * sum + power * 0.693147180559945309;
}

/* 10^(tenths / divisor): 200 turns tenths of a decibel into an amplitude. */
static double decibels(int tenths, unsigned int divisor)
{
    return exponential(2.302585092994045684 * tenths / divisor);
}

/* The precut design.c applies: Auto is the smallest half-decibel step that
 * keeps direct + cross at or below 1. */
static int precut_applied(const struct crossfeed_set *set)
{
    if (set->precut_tenths != CROSSFEED_PRECUT_AUTO)
        return set->precut_tenths;
    double sum = decibels(set->direct_tenths, 200) + decibels(set->cross_tenths, 200);
    if (sum <= 1.0)
        return 0;
    double half_steps = logarithm(sum) * 17.37177927613007; /* 40 / ln 10 */
    int steps = (int)half_steps;
    if (steps < half_steps)
        steps++;
    return -5 * steps;
}

int crossfeed_response(const struct crossfeed_set *set, unsigned int rate, int16_t *tenths,
                       unsigned int points)
{
    int precut = precut_applied(set);
    /* A first-order high shelf lifting the cross gain to the attenuation, g
     * its half gain, the cutoff divided by g to sit at the -3 dB point. */
    double scaler = decibels(set->cross_tenths + precut, 200);
    double g = decibels(set->hf_tenths - set->cross_tenths, 400);
    double s, c;
    sine_cosine(3.141592653589793238 * set->cutoff_hz / (g * rate), &s, &c);
    double b0 = s + c * g, b1 = s - c * g, a0 = s + c / g, a1 = s - c / g;
    double numerator = scaler * scaler * (b0 * b0 + b1 * b1), cross = scaler * scaler * 2.0 * b0 * b1;
    double denominator = a0 * a0 + a1 * a1, across = 2.0 * a0 * a1;
    /* 20 Hz to 20 kHz is three decades; each point is one step of that. */
    double ratio = exponential(6.907755278982137052 / (points - 1)), hz = 20.0;
    for (unsigned int i = 0; i < points; i++) {
        /* |H|^2 of a first-order section is linear in cos(w) over linear in cos(w). */
        sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
        double power = (numerator + cross * c) / (denominator + across * c);
        double db = logarithm(power) * 43.42944819032518;
        tenths[i] = (int16_t)(db + (db < 0.0 ? -0.5 : 0.5));
        hz *= ratio;
    }
    return precut;
}
