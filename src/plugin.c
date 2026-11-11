/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Tangerine Orbit: LV2 plugin entry points. The engine is in engine.h. */
#include "engine.h"
#include "lv2_abi.h"
#include "ports.h"

static LV2_Handle instantiate(const LV2_Descriptor* d, double rate, const char* bundle,
                              const LV2_Feature* const* features)
{
    (void)d;
    (void)bundle;
    (void)features;
    return engine_new(rate);
}

static void connect_port(LV2_Handle h, uint32_t port, void* data)
{
    engine_connect((Plugin*)h, port, (float*)data);
}

static void activate(LV2_Handle h) { engine_activate((Plugin*)h); }

static void run(LV2_Handle h, uint32_t n) { engine_run((Plugin*)h, n); }

static void deactivate(LV2_Handle h) { (void)h; }

static void cleanup(LV2_Handle h) { engine_free((Plugin*)h); }

static const void* extension_data(const char* uri)
{
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    ORBIT_URI, instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
