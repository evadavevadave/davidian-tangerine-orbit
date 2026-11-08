/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Stage 4: the sweep. IC3b is a relaxation oscillator: the Speed pot charges the 10 uF
 * capacitor from the op amp output, and 470k/150k from VREF sets the switching thresholds.
 * 0.01 uF and 150k around the inverting input slow each transition slightly. The capacitor
 * reaches the JFET gates through 3M3, the bias trim through 1M, and 0.05 uF filters the
 * gate bus. These voltages are absolute, not relative to VREF. */
#ifndef LFO_H
#define LFO_H

#include "allpass.h"
#include "circuit.h"

#define LFO_C_TIMING 10e-6
#define LFO_C_FB     10e-9
#define LFO_C_GATE   50e-9
#define LFO_R_IN     150e3
#define LFO_R_POS    470e3
#define LFO_R_REF    150e3
#define LFO_R_POT    500e3
#define LFO_R_SWEEP  3.3e6
#define LFO_R_TRIM   1e6
#define LFO_BETA     (LFO_R_REF / (LFO_R_REF + LFO_R_POS))

/* Each switch takes a moment to regenerate through the 0.01 uF and 150k, and longer on a
 * slower ramp. Measured from this model: period = RC part + 0.059 + 0.010 * ln(RC part). */
#define LFO_DELAY_A  0.059
#define LFO_DELAY_B  0.010

typedef struct {
    double dt;
    double r_pot;
    double v_trim;
    double vc; /* timing capacitor */
    double vq; /* feedback capacitor, output minus inverting input */
    double vg; /* gate bus */
    double vo; /* op amp output */
} Lfo;

/* Pot resistance for a sweep rate, from the charge time between the two thresholds. */
static inline double lfo_pot_for_rate(double hz)
{
    const double th = VREF + LFO_BETA * (OPAMP_HIGH - VREF);
    const double tl = VREF + LFO_BETA * (OPAMP_LOW - VREF);
    const double k = log((OPAMP_HIGH - tl) / (OPAMP_HIGH - th))
                   + log((th - OPAMP_LOW) / (tl - OPAMP_LOW));
    double t = 1.0 / hz, r;
    int i;
    /* Remove the switching delay from the period. This settles in a few rounds. */
    for (i = 0; i < 8; ++i)
        t = 1.0 / hz - LFO_DELAY_A - LFO_DELAY_B * log(t);
    r = t / (k * LFO_C_TIMING);
    if (r > LFO_R_POT)
        r = LFO_R_POT;
    if (r < 1e3)
        r = 1e3;
    return r;
}

static inline void lfo_set_rate(Lfo* l, double hz) { l->r_pot = lfo_pot_for_rate(hz); }

/* position is the 250k trim's wiper, 0 at ground to 1 at VREF. */
static inline void lfo_set_bias(Lfo* l, double position) { l->v_trim = VREF * position; }

/* Starts at the bottom of the sweep with the op amp high. */
static inline void lfo_reset(Lfo* l)
{
    const double gs = 1.0 / LFO_R_SWEEP, gt = 1.0 / LFO_R_TRIM;
    l->vo = OPAMP_HIGH;
    l->vc = VREF + LFO_BETA * (OPAMP_LOW - VREF);
    l->vq = l->vo - l->vc;
    l->vg = (l->vc * gs + l->v_trim * gt) / (gs + gt);
}

static inline void lfo_init(Lfo* l, double fs, double hz, double position)
{
    l->dt = 1.0 / fs;
    lfo_set_rate(l, hz);
    lfo_set_bias(l, position);
    lfo_reset(l);
}

static inline double lfo_vgst(const Lfo* l) { return l->vg - VREF - JFET_VP; }

/* One sample of the oscillator. Clamping the linear solution gives the saturated output
 * and the switching point without tracking them separately. */
static inline void lfo_tick(Lfo* l)
{
    double vo = VREF + l->vq / (1.0 - LFO_BETA);
    double v6, i_in;
    if (vo > OPAMP_HIGH)
        vo = OPAMP_HIGH;
    if (vo < OPAMP_LOW)
        vo = OPAMP_LOW;
    v6 = vo - l->vq;
    i_in = (v6 - l->vc) / LFO_R_IN;
    l->vo = vo;
    l->vq += l->dt * i_in / LFO_C_FB;
    l->vc += l->dt * ((vo - l->vc) / l->r_pot + i_in + (l->vg - l->vc) / LFO_R_SWEEP) / LFO_C_TIMING;
    l->vg += l->dt * ((l->vc - l->vg) / LFO_R_SWEEP + (l->v_trim - l->vg) / LFO_R_TRIM) / LFO_C_GATE;
}

#endif
