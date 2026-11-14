/* SPDX-License-Identifier: GPL-3.0-only */

/* What the two modules of Equalizer agree on. */

#ifndef EQUALIZER_SHARED_H
#define EQUALIZER_SHARED_H
#include <stdint.h>

enum { EQ_BANDS = 5 };

enum eq_type { EQ_OFF, EQ_PEAKING, EQ_LOW_SHELF, EQ_HIGH_SHELF };

/* The Custom preset is EQ_VALUES choices, one byte each: the precut, then each
 * band's type, frequency, Q and gain. The order and the choices are the saved
 * format. */
enum {
    EQ_PRECUT,    /* 0 is Auto, then 0.0 to -10.0 dB in tenths */
    EQ_TYPE,      /* enum eq_type */
    EQ_FREQUENCY, /* an index into eq_frequencies */
    EQ_Q,         /* 0.3 to 4.0 in tenths */
    EQ_GAIN,      /* -12.0 to +12.0 dB in halves */
    EQ_PARAMETERS,
    EQ_VALUES = 1 + EQ_BANDS * (EQ_PARAMETERS - 1),
};

#define KEY_VALUES 1       /* blob: the EQ_VALUES choices */
#define KEY_CUSTOM_SHOWN 2 /* bool: Custom is the preset that plays; shows the Settings row */

enum { EQ_PRECUT_AUTO = 0 };

static const uint8_t eq_choices[EQ_PARAMETERS] = {102, 4, 32, 38, 49};

static const uint16_t eq_frequencies[] = {
    20,  25,  31,   40,   50,   60,   70,   80,   100,  125,  160,  200,   250,   315,   400,   500,
    630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000,
};

/* Where a band's parameter is among the choices; the precut has no band. */
static inline unsigned int eq_slot(unsigned int band, unsigned int parameter)
{
    return parameter == EQ_PRECUT ? 0 : parameter + band * (EQ_PARAMETERS - 1);
}

/* Choices as tenths: of a decibel for precut (below zero) and gain, of one for Q. */
static inline int eq_precut_tenths(unsigned int choice)
{
    return 1 - (int)choice;
}
static inline int eq_q_tenths(unsigned int choice)
{
    return 3 + (int)choice;
}
static inline int eq_gain_tenths(unsigned int choice)
{
    return (int)choice * 5 - 120;
}

/* What main passes the editor screen as its argument. The editor module links
 * none of the filter code; it calls main's. */
struct eq_link {
    const uint8_t *values;   /* the EQ_VALUES choices that play */
    const uint8_t *defaults; /* the choices a reset returns to */
    /* Plays one choice changed. Nothing changes when a band then has no stable filter. */
    void (*change)(unsigned int slot, unsigned int choice);
    /* Plays a whole set of choices, under the same rule. */
    void (*set)(const uint8_t *choices);
    void (*save)(void); /* stores the choices */
    /* What plays, at `points` frequencies from 20 Hz to 20 kHz evenly spaced in
     * octaves, in tenths of a decibel. Returns the precut applied, in tenths
     * of a decibel. */
    unsigned int (*response)(int16_t *tenths, unsigned int points);
};

#endif
