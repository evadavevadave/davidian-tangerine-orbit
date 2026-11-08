/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Stage 3: 2N4125 PNP inverting summer. Dry and wet each come in through 150k, 150k runs
 * from collector to base, 56k from collector to ground, and the emitter sits on VREF.
 * Then 0.05 uF and 150k to the output jack. */
#ifndef MIX_AMP_H
#define MIX_AMP_H

#include "circuit.h"

#define MIX_RS    150e3
#define MIX_RF    150e3
#define MIX_RC    56e3
#define MIX_CO    50e-9
#define MIX_RO    150e3

/* Typical small-signal PNP values. Estimates, not from the schematic. */
#define BJT_IS    1e-14
#define BJT_VT    0.0259
#define BJT_BETA  150.0
#define BJT_VCMAX (-0.15) /* collector limit below the emitter, relative to VREF */
#define BJT_SOFT  0.02

typedef struct {
    double x;  /* emitter-base voltage, kept as the next starting guess */
    double g;  /* 2C*fs of the output capacitor */
    double s;  /* output capacitor state */
} MixAmp;

/* Finds the collector voltage. The base equation always rises with x, so Newton is
 * safe inside a bracket, falling back to bisection if a step leaves it. */
static inline double mix_amp_solve(MixAmp* m, double dry, double wet)
{
    const double gs = 1.0 / MIX_RC + 1.0 / MIX_RF;
    const double sum = dry + wet;
    double lo = -6.0, hi = 6.0, x = m->x, vc = 0.0;
    int i;
    for (i = 0; i < 48; ++i) {
        double ic = BJT_IS * exp(x / BJT_VT);
        double vcu = (ic - VREF / MIX_RC - x / MIX_RF) / gs;
        double dvcu = (ic / BJT_VT - 1.0 / MIX_RF) / gs;
        double y = BJT_VCMAX - vcu;
        double r = sqrt(y * y + BJT_SOFT * BJT_SOFT);
        double dvc = 0.5 * (1.0 + y / r) * dvcu;
        double ica, dica, f, df, nx;
        vc = BJT_VCMAX - 0.5 * (y + r);
        ica = (vc + VREF) / MIX_RC + (vc + x) / MIX_RF;
        dica = dvc * gs + 1.0 / MIX_RF;
        f = (sum + 2.0 * x) / MIX_RS + (vc + x) / MIX_RF + ica / BJT_BETA;
        df = 2.0 / MIX_RS + (dvc + 1.0) / MIX_RF + dica / BJT_BETA;
        if (f > 0.0)
            hi = x;
        else
            lo = x;
        nx = x - f / df;
        if (!(nx > lo && nx < hi))
            nx = 0.5 * (lo + hi);
        if (fabs(nx - x) < 1e-11) {
            x = nx;
            break;
        }
        x = nx;
    }
    m->x = x;
    return vc;
}

/* Settles the transistor at idle and charges the output capacitor to match, so starting
 * up makes no thump. */
static inline void mix_amp_reset(MixAmp* m)
{
    m->x = 0.57;
    m->s = m->g * mix_amp_solve(m, 0.0, 0.0);
}

static inline void mix_amp_init(MixAmp* m, double fs)
{
    m->g = 2.0 * MIX_CO * fs;
    mix_amp_reset(m);
}

static inline double mix_amp_tick(MixAmp* m, double dry, double wet)
{
    const double vc = mix_amp_solve(m, dry, wet);
    const double i = (m->g * vc - m->s) / (1.0 + m->g * MIX_RO);
    const double vcap = vc - i * MIX_RO;
    m->s = 2.0 * m->g * vcap - m->s;
    return i * MIX_RO;
}

#endif
