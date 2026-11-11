/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* LV2 GUI for the Davidian Tangerine Orbit: the LV2 UI entry points for Win32
 * (ui:WindowsUI), X11 (ui:X11UI) and macOS Cocoa (ui:CocoaUI). The window layer is in
 * window.h and the panel in panel.h. The UI ABI is declared here so no LV2 SDK is needed.
 * On macOS, build this file as Objective-C without ARC. */
#define PANEL_VIEW_CLASS DavidianTangerineOrbitView
#include "window.h"

#include "../lv2_abi.h"

/* ---- LV2 UI ABI (layout and names match lv2/ui/ui.h, Copyright 2009-2016 David Robillard
 * and Lars Luthman, ISC license; see NOTICE.md) ---- */

typedef void* LV2UI_Widget;
typedef void* LV2UI_Handle;
typedef void* LV2UI_Controller;
typedef void* LV2UI_Feature_Handle;
typedef void (*LV2UI_Write_Function)(LV2UI_Controller controller, uint32_t port_index,
                                     uint32_t buffer_size, uint32_t port_protocol,
                                     const void* buffer);
typedef struct LV2UI_Descriptor {
    const char* URI;
    LV2UI_Handle (*instantiate)(const struct LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features);
    void (*cleanup)(LV2UI_Handle ui);
    void (*port_event)(LV2UI_Handle ui, uint32_t port_index, uint32_t buffer_size,
                       uint32_t format, const void* buffer);
    const void* (*extension_data)(const char* uri);
} LV2UI_Descriptor;
typedef struct { int (*idle)(LV2UI_Handle ui); } LV2UI_Idle_Interface;
typedef struct {
    LV2UI_Feature_Handle handle;
    int (*ui_resize)(LV2UI_Feature_Handle handle, int width, int height);
} LV2UI_Resize;

#define LV2_UI__parent        "http://lv2plug.in/ns/extensions/ui#parent"
#define LV2_UI__resize        "http://lv2plug.in/ns/extensions/ui#resize"
#define LV2_UI__idleInterface "http://lv2plug.in/ns/extensions/ui#idleInterface"

typedef struct {
    PanelWindow win; /* first, so a PanelWindow* is also a UI* */
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
} UI;

/* ---- LV2 UI entry points ---- */

static void host_write(void* host, int port, float value)
{
    UI* ui = (UI*)host;
    if (ui->write)
        ui->write(ui->controller, (uint32_t)port, sizeof(float), 0, &value);
}

static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features)
{
    UI* ui;
    void* parent = NULL;
    const LV2UI_Resize* resize = NULL;
    int i;
    (void)descriptor;
    (void)bundle_path;
    if (!plugin_uri || strcmp(plugin_uri, ORBIT_URI) != 0)
        return NULL;
    for (i = 0; features && features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_UI__parent))
            parent = features[i]->data;
        else if (!strcmp(features[i]->URI, LV2_UI__resize))
            resize = (const LV2UI_Resize*)features[i]->data;
    }
    ui = (UI*)calloc(1, sizeof(UI));
    if (!ui)
        return NULL;
    ui->write = write_function;
    ui->controller = controller;
    if (!panel_init(&ui->win.panel, host_write, ui)) {
        free(ui);
        return NULL;
    }
    if (!window_open(&ui->win, parent)) {
        panel_free(&ui->win.panel);
        free(ui);
        return NULL;
    }
    if (resize)
        resize->ui_resize(resize->handle, PANEL_W, PANEL_H);
    *widget = window_widget(&ui->win);
    return ui;
}

static void cleanup(LV2UI_Handle h)
{
    UI* ui = (UI*)h;
    window_close(&ui->win);
    panel_free(&ui->win.panel);
    free(ui);
}

/* The host sends control values here, including the sweep output for the lamp. */
static void port_event(LV2UI_Handle h, uint32_t port, uint32_t size, uint32_t format, const void* buffer)
{
    UI* ui = (UI*)h;
    if (format != 0 || size != sizeof(float))
        return;
    panel_set(&ui->win.panel, (int)port, *(const float*)buffer);
    pw_redraw(&ui->win);
}

static int idle(LV2UI_Handle h) { return window_idle(&((UI*)h)->win); }

static const LV2UI_Idle_Interface idle_iface = { idle };

static const void* extension_data(const char* uri)
{
    return !strcmp(uri, LV2_UI__idleInterface) ? &idle_iface : NULL;
}

static const LV2UI_Descriptor ui_descriptor = {
    ORBIT_UI_URI, instantiate, cleanup, port_event, extension_data
};

LV2_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &ui_descriptor : NULL;
}
