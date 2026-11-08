/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Stage 1: 10k and 0.01 uF into IC1a, a unity follower with 470k to VREF. */
#ifndef INPUT_BUFFER_H
#define INPUT_BUFFER_H

#include "circuit.h"

#define IN_R_SERIES 10e3
#define IN_R_BIAS   470e3
#define IN_C        10e-9

typedef struct {
    double g; /* trapezoidal capacitor conductance, 2C*fs */
    double s; /* capacitor state */
} InputBuffer;

static inline void input_buffer_init(InputBuffer* b, double fs)
{
    b->g = 2.0 * IN_C * fs;
    b->s = 0.0;
}

static inline double input_buffer_tick(InputBuffer* b, double vin)
{
    const double r = IN_R_SERIES + IN_R_BIAS;
    const double i = (b->g * vin - b->s) / (1.0 + b->g * r);
    const double vcap = vin - i * r;
    b->s = 2.0 * b->g * vcap - b->s;
    return opamp_limit(i * IN_R_BIAS);
}

#endif
