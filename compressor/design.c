/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox lib/rbcodec/dsp/compressor.c.
 *
 * Copyright (C) 2009 Jeffrey Goode
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* The curve as the pump task uses it: a gain for each of the 64 input levels
 * the top six bits of a sample pick out, with the one-pole coefficients at the
 * sample rate. Decibels become gains through 2^x: the whole part a shift, the
 * fraction a short table, so no exponential is needed. */

#include "compressor.h"

enum {
    UNITY = 1 << 24,
    DELAY_MS = 3, /* the look-ahead, which is also the limiter's attack */
    /* The sidechain high-pass filters' 1/RC, as Rockbox writes them: cutoffs
     * near 237 Hz and 2.18 kHz. */
    HP1_RC = 0x003C1,
    HP2_RC = 0x02065,
    /* 1000 * 10 / (20 log10 e): the time constant becomes a 10 dB change per period. */
    PER_TEN_DB = 1152,
    LOG2_10_OVER_20_Q24 = 2786707,
};

/* -20 log10(i / 64) for i from 1 to 63, S15.16; entry 0 stands for silence. */
static const uint32_t level_db[64] = {
    0x000000, 0x241FA4, 0x1E1A5E, 0x1A94C8, 0x181518, 0x1624EA, 0x148F82, 0x1338BD,
    0x120FD2, 0x1109EB, 0x101FA4, 0x0F4BB6, 0x0E8A3C, 0x0DD840, 0x0D3377, 0x0C9A0E,
    0x0C0A8C, 0x0B83BE, 0x0B04A5, 0x0A8C6C, 0x0A1A5E, 0x09ADE1, 0x094670, 0x08E398,
    0x0884F6, 0x082A30, 0x07D2FA, 0x077F0F, 0x072E31, 0x06E02A, 0x0694C8, 0x064BDF,
    0x060546, 0x05C0DA, 0x057E78, 0x053E03, 0x04FF5F, 0x04C273, 0x048726, 0x044D64,
    0x041518, 0x03DE30, 0x03A89B, 0x037448, 0x03412A, 0x030F32, 0x02DE52, 0x02AE80,
    0x027FB0, 0x0251D6, 0x0224EA, 0x01F8E2, 0x01CDB4, 0x01A359, 0x0179C9, 0x0150FC,
    0x0128EB, 0x010190, 0x00DAE4, 0x00B4E1, 0x008F82, 0x006AC1, 0x004699, 0x002305,
};

/* 2^(i / 64) for i from 0 to 64, Q30. */
static const uint32_t pow2[65] = {
    1073741824, 1085434106, 1097253708, 1109202018, 1121280436, 1133490379, 1145833280,
    1158310587, 1170923762, 1183674286, 1196563654, 1209593378, 1222764986, 1236080024,
    1249540052, 1263146652, 1276901417, 1290805962, 1304861917, 1319070932, 1333434672,
    1347954824, 1362633090, 1377471191, 1392470869, 1407633882, 1422962010, 1438457051,
    1454120821, 1469955159, 1485961921, 1502142985, 1518500250, 1535035634, 1551751076,
    1568648537, 1585730000, 1602997467, 1620452965, 1638098541, 1655936265, 1673968228,
    1692196547, 1710623359, 1729250827, 1748081133, 1767116489, 1786359126, 1805811301,
    1825475297, 1845353420, 1865448001, 1885761398, 1906295993, 1927054196, 1948038440,
    1969251188, 1990694927, 2012372174, 2034285470, 2056437387, 2078830522, 2101467502,
    2124350982, 2147483648,
};

/* 10^(db / 20) as S7.24, for S15.16 decibels from -96 up to +36 dB. */
static int32_t gain_from_db(int32_t db)
{
    int64_t x = ((int64_t)db * LOG2_10_OVER_20_Q24) >> 24; /* log2 of the gain, S15.16 */
    int whole = (int)(x >> 16);
    uint32_t fraction = (uint32_t)x & 0xffff;
    uint32_t i = fraction >> 10, rest = fraction & 0x3ff;
    uint32_t v = pow2[i] + (uint32_t)(((uint64_t)(pow2[i + 1] - pow2[i]) * rest) >> 10);
    return (int32_t)(v >> (6 - whole));
}

/* Rockbox's one-pole coefficient for a 10 dB change over `ms`. */
static int32_t per_ms_coefficient(unsigned int rate, unsigned int ms)
{
    return (int32_t)(UNITY / rate * PER_TEN_DB / ms);
}

void compressor_design(struct compressor_design *out, const struct compressor_set *set,
                       unsigned int rate)
{
    struct compressor_points p;
    compressor_points(set, &p);

    out->gain[0] = UNITY;
    for (unsigned int i = 1; i < 64; i++) {
        int32_t db = -(int32_t)level_db[i], offset;
        if (db <= p.db[0]) {
            offset = 0;
        } else if (set->knee && db <= p.db[2]) {
            /* Across the knee: its 6 dB width as a fraction, S15.16. */
            int32_t across = (db - p.db[0]) / 6;
            offset = (int32_t)(((int64_t)across * p.offset[2]) >> 16);
        } else {
            /* From the threshold up to full scale as a fraction, S15.16; the
             * threshold is whole decibels, so this is exact. */
            int32_t above = (p.db[1] - db) / set->threshold_db;
            offset = (int32_t)(((int64_t)above * p.offset[3]) >> 16);
        }
        out->gain[i] = gain_from_db(offset);
    }
    out->gain[64] = gain_from_db(p.offset[3]);
    out->gain[65] = gain_from_db(p.offset[4]);
    out->makeup = gain_from_db(p.makeup);

    out->release_a = per_ms_coefficient(rate, set->release_ms);
    out->release_b = UNITY - out->release_a;
    if (set->attack_ms) {
        out->attack_a = per_ms_coefficient(rate, set->attack_ms);
        out->attack_b = UNITY - out->attack_a;
    } else {
        out->attack_a = UNITY;
        out->attack_b = 0;
    }
    out->limit_a = per_ms_coefficient(rate, DELAY_MS);
    /* y[n] = a (y[n-1] + x[n] - x[n-1]) with a = rate / (rate + 1/RC). */
    out->hp1 = (int32_t)(UNITY / (rate + HP1_RC) * rate);
    out->hp2 = (int32_t)(UNITY / (rate + HP2_RC) * rate);
}
