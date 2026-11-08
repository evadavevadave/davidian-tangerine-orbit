/* Stage 2: the four all-pass stages (IC1b, IC2a, IC2b, IC3a), each with a 2N5952 JFET
 * and 22k across it, plus the 22k from stage 4's output to stage 1's inverting input.
 *
 * Each stage's non-inverting node is solved with the JFET's nonlinear channel, and the
 * feedback loop is solved within the sample, so it adds no delay. */
#ifndef ALLPASS_H
#define ALLPASS_H

#include "circuit.h"

#define AP_C   50e-9
#define AP_R   22e3
#define AP_RI  10e3 /* input and feedback resistors around each op amp */
#define AP_RFB 22e3 /* stage 4 to stage 1's inverting input */

/* 2N5952 datasheet ranges are Vp -1.3 to -3.5 V, Idss 1 to 5 mA. These are mid-range picks. */
#define JFET_VP   (-2.0)
#define JFET_IDSS 2.5e-3
#define JFET_BETA (JFET_IDSS / (JFET_VP * JFET_VP))
#define JFET_SOFT 0.02 /* rounds the turn-off corner, in volts */

#define AP_MAX_ITER 6
#define AP_TOL      1e-7

typedef struct {
    double g;    /* 2C*fs */
    double s[4]; /* capacitor states */
    double v[4]; /* non-inverting node voltages, kept as the next starting guess */
} Allpass;

static inline void allpass_reset(Allpass* a)
{
    int i;
    for (i = 0; i < 4; ++i)
        a->s[i] = a->v[i] = 0.0;
}

static inline void allpass_init(Allpass* a, double fs)
{
    a->g = 2.0 * AP_C * fs;
    allpass_reset(a);
}

/* Drain current for drain-source voltage v and overdrive vgst (Vgs - Vp), with its slope.
 * For negative v the drain and source swap roles, which is what makes the channel nonlinear. */
static inline double jfet_current(double v, double vgst, double* slope)
{
    const double u = fabs(v);
    const double x = v >= 0.0 ? vgst : vgst + u;
    const double r = sqrt(x * x + JFET_SOFT * JFET_SOFT);
    const double vov = 0.5 * (x + r);
    const double dvov = v >= 0.0 ? 0.0 : 0.5 * (1.0 + x / r);
    double i;
    if (u < vov) {
        i = JFET_BETA * (2.0 * vov * u - u * u);
        *slope = 2.0 * JFET_BETA * (vov - u + u * dvov);
    } else {
        i = JFET_BETA * vov * vov;
        *slope = 2.0 * JFET_BETA * vov * dvov;
    }
    return v >= 0.0 ? i : -i;
}

/* Returns stage 4's output for input `in`, the buffered dry signal. */
static inline double allpass_tick(Allpass* a, double in, double vgst)
{
    const double g = a->g;
    const double gr = 1.0 / AP_R;
    const double fb = AP_RI / AP_RFB;
    double v1 = a->v[0], k, out1 = 0.0, vf = 0.0;
    double in_k[4];
    int it, k_i;

    /* Stage 1's non-inverting node sees no feedback, so it solves on its own. */
    for (it = 0; it < AP_MAX_ITER; ++it) {
        double gd, id = jfet_current(v1, vgst, &gd);
        double next = (g * in - a->s[0] - (id - gd * v1)) / (g + gr + gd);
        double dv = next - v1;
        v1 = next;
        if (fabs(dv) < AP_TOL)
            break;
    }
    a->v[0] = v1;
    k = (2.0 + fb) * v1 - in; /* stage 1 output before the feedback term */

    /* Stages 2 to 4 are linearised around their last values and the loop solved directly.
     * Repeating this is Newton's method on the whole loop. */
    for (it = 0; it < AP_MAX_ITER; ++it) {
        double p[4], q[4], ca = 1.0, cb = 0.0, change = 0.0, x;
        for (k_i = 1; k_i < 4; ++k_i) {
            double gd, id = jfet_current(a->v[k_i], vgst, &gd);
            double d = g + gr + gd;
            p[k_i] = g / d;
            q[k_i] = (-a->s[k_i] - (id - gd * a->v[k_i])) / d;
            /* Stage output is 2*v - input, chained: out = ca*out1 + cb. */
            cb = (2.0 * p[k_i] - 1.0) * cb + 2.0 * q[k_i];
            ca = (2.0 * p[k_i] - 1.0) * ca;
        }
        vf = (ca * k + cb) / (1.0 + fb * ca);
        out1 = k - fb * vf;
        x = out1;
        for (k_i = 1; k_i < 4; ++k_i) {
            double nv = p[k_i] * x + q[k_i];
            change += fabs(nv - a->v[k_i]);
            a->v[k_i] = nv;
            in_k[k_i] = x;
            x = 2.0 * nv - x;
        }
        if (change < AP_TOL)
            break;
    }

    in_k[0] = in;
    for (k_i = 0; k_i < 4; ++k_i)
        a->s[k_i] = 2.0 * g * (in_k[k_i] - a->v[k_i]) - a->s[k_i];
    return vf;
}

#endif
