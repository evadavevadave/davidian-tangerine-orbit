/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Tangerine Orbit: the audio engine shared by the LV2, VST3 and AU wrappers.
 * The host-facing state is an array of float pointers indexed like the LV2 ports (ports.h);
 * each wrapper points them at its own buffers and parameter values. The circuit model is
 * in phaser.h and lfo.h. */
#ifndef ENGINE_H
#define ENGINE_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "lfo.h"
#include "phaser.h"
#include "ports.h"

/* Flush denormals to zero while processing, so decaying states can't stall the CPU. */
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#    include <xmmintrin.h>
typedef unsigned FpState;
static FpState fp_enter(void)
{
    const FpState s = _mm_getcsr();
    _mm_setcsr(s | 0x8040); /* FTZ | DAZ */
    return s;
}
static void fp_leave(FpState s) { _mm_setcsr(s); }
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
typedef unsigned long long FpState;
static FpState fp_enter(void)
{
    FpState s;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(s));
    __asm__ __volatile__("msr fpcr, %0" : : "r"(s | (1ULL << 24))); /* FZ */
    return s;
}
static void fp_leave(FpState s) { __asm__ __volatile__("msr fpcr, %0" : : "r"(s)); }
#else
typedef int FpState;
static FpState fp_enter(void) { return 0; }
static void fp_leave(FpState s) { (void)s; }
#endif

#define SMOOTH_SECONDS 0.01
#define BYPASS_SECONDS 0.02

/* Control defaults: must match bundle/tangerine_orbit.ttl. */
#define DEF_SPEED  0.5f
#define DEF_BIAS   0.46f
#define DEF_VOLTS  1.0f
#define DEF_OUTPUT (-6.0f)

typedef struct {
    float* port[PORT_COUNT];
    double rate;
    double smooth;
    double fade_step;
    double volts_now;
    double output_now;
    double fade;
    Phaser phaser;
    Lfo lfo;
} Plugin;

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* A port's value, or def if it isn't connected or isn't a number. */
static float port_or(const Plugin* p, int i, float def)
{
    const float v = p->port[i] ? *p->port[i] : def;
    return isfinite(v) ? v : def;
}

static double target_volts(const Plugin* p) { return clampd(port_or(p, PORT_VOLTS, DEF_VOLTS), 0.1, 10.0); }

static double target_gain(const Plugin* p)
{
    return pow(10.0, clampd(port_or(p, PORT_OUTPUT, DEF_OUTPUT), -24.0, 12.0) / 20.0);
}

/* The effect is in when both the host's bypass and the footswitch say so. */
static double fade_target(const Plugin* p)
{
    return port_or(p, PORT_ENABLED, 1.0f) > 0.5f && port_or(p, PORT_ENGAGE, 1.0f) > 0.5f ? 1.0 : 0.0;
}

static void read_controls(Plugin* p)
{
    lfo_set_rate(&p->lfo, clampd(port_or(p, PORT_SPEED, DEF_SPEED), 0.2, 10.0));
    lfo_set_bias(&p->lfo, clampd(port_or(p, PORT_BIAS, DEF_BIAS), 0.0, 1.0));
}

/* Timing capacitor voltage between the switching thresholds, for the GUI's sweep lamp. */
static float sweep_position(const Lfo* l)
{
    const double th = VREF + LFO_BETA * (OPAMP_HIGH - VREF);
    const double tl = VREF + LFO_BETA * (OPAMP_LOW - VREF);
    return (float)clampd((l->vc - tl) / (th - tl), 0.0, 1.0);
}

/* The rate is fixed for the life of the engine: make a new one when it changes. */
static Plugin* engine_new(double rate)
{
    Plugin* p = (Plugin*)calloc(1, sizeof(Plugin));
    if (!p)
        return NULL;
    p->rate = rate;
    p->smooth = 1.0 - exp(-1.0 / (SMOOTH_SECONDS * rate));
    p->fade_step = 1.0 / (BYPASS_SECONDS * rate);
    phaser_init(&p->phaser, rate);
    lfo_init(&p->lfo, rate, DEF_SPEED, DEF_BIAS);
    return p;
}

/* The circuit has no lookahead or oversampling, so there is no latency. */
static int engine_latency(const Plugin* p) { (void)p; return 0; }

static void engine_connect(Plugin* p, uint32_t port, float* data)
{
    if (port < PORT_COUNT)
        p->port[port] = data;
}

/* Clears the circuit and starts the sweep at the bottom; smoothed values jump to target. */
static void engine_activate(Plugin* p)
{
    read_controls(p);
    lfo_reset(&p->lfo);
    phaser_reset(&p->phaser);
    p->volts_now = target_volts(p);
    p->output_now = target_gain(p);
    p->fade = fade_target(p);
}

/* PORT_IN and PORT_OUT may point at the same buffer. */
static void engine_run(Plugin* p, uint32_t n)
{
    const float* in = p->port[PORT_IN];
    float* out = p->port[PORT_OUT];
    const double volts = target_volts(p);
    const double gain = target_gain(p);
    const double fade_to = fade_target(p);
    uint32_t i;
    const FpState fp = fp_enter();

    read_controls(p);
    if (in && out) {
        for (i = 0; i < n; ++i) {
            double x = in[i];
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
            if (!isfinite(y)) { /* never pass garbage to the host; restart the model */
                phaser_reset(&p->phaser);
                y = 0.0;
            }
            out[i] = (float)((1.0 - p->fade) * x + p->fade * y);
        }
    }
    if (p->port[PORT_SWEEP])
        *p->port[PORT_SWEEP] = sweep_position(&p->lfo);
    fp_leave(fp);
}

static void engine_free(Plugin* p) { free(p); }

#endif
