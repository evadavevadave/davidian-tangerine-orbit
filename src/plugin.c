/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* LV2 entry points, controls and bypass for the Tangerine Orbit circuit model. */
#include <stdlib.h>

#include "lfo.h"
#include "lv2_abi.h"
#include "phaser.h"
#include "ports.h"

#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#    define ORBIT_HAS_CSR 1
#endif

#define SMOOTH_SECONDS 0.01
#define BYPASS_SECONDS 0.02

typedef struct {
    const float* in;
    float*       out;
    const float* speed;
    const float* bias;
    const float* volts;
    const float* output;
    const float* enabled;
    const float* engage;
    float*       sweep;

    double fs;
    double smooth;
    double fade_step;
    double volts_now;
    double output_now;
    double fade;

    Phaser phaser;
    Lfo    lfo;
} Plugin;

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double sample_rate,
                              const char* bundle_path, const LV2_Feature* const* features)
{
    Plugin* p = (Plugin*)calloc(1, sizeof(Plugin));
    (void)descriptor;
    (void)bundle_path;
    (void)features;
    if (!p)
        return NULL;
    p->fs = sample_rate;
    p->smooth = 1.0 - exp(-1.0 / (SMOOTH_SECONDS * sample_rate));
    p->fade_step = 1.0 / (BYPASS_SECONDS * sample_rate);
    phaser_init(&p->phaser, sample_rate);
    lfo_init(&p->lfo, sample_rate, 0.5, 0.46);
    return p;
}

static void connect_port(LV2_Handle h, uint32_t port, void* data)
{
    Plugin* p = (Plugin*)h;
    switch (port) {
    case PORT_IN:      p->in = (const float*)data; break;
    case PORT_OUT:     p->out = (float*)data; break;
    case PORT_SPEED:   p->speed = (const float*)data; break;
    case PORT_BIAS:    p->bias = (const float*)data; break;
    case PORT_VOLTS:   p->volts = (const float*)data; break;
    case PORT_OUTPUT:  p->output = (const float*)data; break;
    case PORT_ENABLED: p->enabled = (const float*)data; break;
    case PORT_ENGAGE:  p->engage = (const float*)data; break;
    case PORT_SWEEP:   p->sweep = (float*)data; break;
    default: break;
    }
}

static double output_gain(const Plugin* p)
{
    return pow(10.0, clampd(*p->output, -24.0, 12.0) / 20.0);
}

/* The effect is in when both the host's bypass and the footswitch say so. */
static double fade_target(const Plugin* p)
{
    return *p->enabled > 0.5f && *p->engage > 0.5f ? 1.0 : 0.0;
}

/* Timing capacitor voltage between the switching thresholds, for the GUI's sweep lamp. */
static float sweep_position(const Lfo* l)
{
    const double th = VREF + LFO_BETA * (OPAMP_HIGH - VREF);
    const double tl = VREF + LFO_BETA * (OPAMP_LOW - VREF);
    return (float)clampd((l->vc - tl) / (th - tl), 0.0, 1.0);
}

static void activate(LV2_Handle h)
{
    Plugin* p = (Plugin*)h;
    lfo_set_rate(&p->lfo, clampd(*p->speed, 0.2, 10.0));
    lfo_set_bias(&p->lfo, clampd(*p->bias, 0.0, 1.0));
    lfo_reset(&p->lfo);
    phaser_reset(&p->phaser);
    p->volts_now = clampd(*p->volts, 0.1, 10.0);
    p->output_now = output_gain(p);
    p->fade = fade_target(p);
}

static void run(LV2_Handle h, uint32_t n)
{
    Plugin* p = (Plugin*)h;
    const double volts = clampd(*p->volts, 0.1, 10.0);
    const double gain = output_gain(p);
    const double fade_to = fade_target(p);
    uint32_t i;
#ifdef ORBIT_HAS_CSR
    const unsigned int csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040); /* flush denormals so decaying signals don't slow the CPU */
#endif

    lfo_set_rate(&p->lfo, clampd(*p->speed, 0.2, 10.0));
    lfo_set_bias(&p->lfo, clampd(*p->bias, 0.0, 1.0));

    for (i = 0; i < n; ++i) {
        double x = p->in[i];
        double y;
        if (!isfinite(x))
            x = 0.0;
        p->volts_now += p->smooth * (volts - p->volts_now);
        p->output_now += p->smooth * (gain - p->output_now);
        if (p->fade < fade_to)
            p->fade = fade_to < p->fade + p->fade_step ? fade_to : p->fade + p->fade_step;
        else if (p->fade > fade_to)
            p->fade = fade_to > p->fade - p->fade_step ? fade_to : p->fade - p->fade_step;

        /* The circuit keeps running in bypass so the sweep doesn't stop, as on the pedal. */
        lfo_tick(&p->lfo);
        y = phaser_tick(&p->phaser, x * p->volts_now, lfo_vgst(&p->lfo)) / p->volts_now * p->output_now;
        if (!isfinite(y)) {
            phaser_reset(&p->phaser);
            y = 0.0;
        }
        p->out[i] = (float)((1.0 - p->fade) * x + p->fade * y);
    }
    if (p->sweep)
        *p->sweep = sweep_position(&p->lfo);

#ifdef ORBIT_HAS_CSR
    _mm_setcsr(csr);
#endif
}

static void deactivate(LV2_Handle h) { (void)h; }
static void cleanup(LV2_Handle h) { free(h); }
static const void* extension_data(const char* uri) { (void)uri; return NULL; }

static const LV2_Descriptor descriptor = {
    ORBIT_URI, instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
