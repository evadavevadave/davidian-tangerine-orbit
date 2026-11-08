/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The audio path in schematic order. */
#ifndef PHASER_H
#define PHASER_H

#include "allpass.h"
#include "input_buffer.h"
#include "mix_amp.h"

typedef struct {
    InputBuffer input;
    Allpass     chain;
    MixAmp      mix;
} Phaser;

static inline void phaser_init(Phaser* p, double fs)
{
    input_buffer_init(&p->input, fs);
    allpass_init(&p->chain, fs);
    mix_amp_init(&p->mix, fs);
}

static inline void phaser_reset(Phaser* p)
{
    p->input.s = 0.0;
    allpass_reset(&p->chain);
    mix_amp_reset(&p->mix);
}

/* vin is in circuit volts; vgst is the JFET overdrive (Vgs - Vp) from the LFO. */
static inline double phaser_tick(Phaser* p, double vin, double vgst)
{
    const double dry = input_buffer_tick(&p->input, vin);
    const double wet = opamp_limit(allpass_tick(&p->chain, dry, vgst));
    return mix_amp_tick(&p->mix, dry, wet);
}

#endif
