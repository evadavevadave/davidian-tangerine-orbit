/* SPDX-License-Identifier: ISC
 * Interface declarations matching lv2/core/lv2.h, Copyright 2006-2020 Steve Harris and
 * David Robillard, ISC license (see NOTICE.md). Reproduced so the build needs no LV2 SDK. */
/* The small, stable part of the LV2 core C ABI this plugin uses.
 * Declared here so the build needs no LV2 SDK. Layout and names match lv2/core/lv2.h. */
#ifndef LV2_ABI_H
#define LV2_ABI_H

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#    define LV2_EXPORT __declspec(dllexport)
#else
#    define LV2_EXPORT __attribute__((visibility("default")))
#endif

typedef void* LV2_Handle;

typedef struct {
    const char* URI;
    void*       data;
} LV2_Feature;

typedef struct LV2_Descriptor {
    const char* URI;
    LV2_Handle (*instantiate)(const struct LV2_Descriptor* descriptor,
                              double                       sample_rate,
                              const char*                  bundle_path,
                              const LV2_Feature* const*    features);
    void (*connect_port)(LV2_Handle instance, uint32_t port, void* data);
    void (*activate)(LV2_Handle instance);
    void (*run)(LV2_Handle instance, uint32_t sample_count);
    void (*deactivate)(LV2_Handle instance);
    void (*cleanup)(LV2_Handle instance);
    const void* (*extension_data)(const char* uri);
} LV2_Descriptor;

#endif
