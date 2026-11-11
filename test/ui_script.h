/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI checks shared by ui_check.c (Win32 LV2), vst3_check.c (Win32 VST3),
 * ui_check_mac.m (Cocoa LV2) and au_check_mac.m (Cocoa AU). The including file defines
 * check(), the last_port/last_value/writes globals filled by its write callback, WIDGET_T,
 * and these mouse helpers in panel coordinates (top-left origin):
 *   click(w, x, y), drag(w, x, y, dy), dblclick(w, x, y), wheel_up(w, x, y).
 * Widget positions come from the panel itself, so a layout change can't break the test. */
#ifndef UI_SCRIPT_H
#define UI_SCRIPT_H

#include <math.h>
#include <stdio.h>

#include "../src/ports.h"
#include "../src/ui/panel.h"

/* Port values as a host sends them on opening: the defaults in tangerine_orbit.ttl, with
 * the sweep lamp part-way up. */
static const float UI_DEFAULTS[PORT_COUNT] = { 0, 0, 0.5f, 0.46f, 1.0f, -6.0f, 1, 1, 0.6f };

#define WX(wd) ((int)(wd)->x)
#define WY(wd) ((int)(wd)->y)

static void ui_script(WIDGET_T w)
{
    const Widget* speed = &WIDGETS[0];
    const Widget* bias = &WIDGETS[1];
    const Widget* output = &WIDGETS[3];
    const Widget* foot = &WIDGETS[4];
    char label[160];

    /* 180 px is a knob's full travel, so 36 px down is a fifth of it. */
    drag(w, WX(bias), WY(bias), 36);
    snprintf(label, sizeof label, "dragging Bias down 36 px sends port %d = %.3f", last_port, last_value);
    check(label, last_port == PORT_BIAS && fabsf(last_value - 0.26f) < 0.01f);

    drag(w, WX(speed), WY(speed), -400);
    snprintf(label, sizeof label, "dragging Speed far up stops at %.3f Hz", last_value);
    check(label, last_port == PORT_SPEED && fabsf(last_value - 10.0f) < 1e-4f);

    dblclick(w, WX(speed), WY(speed));
    snprintf(label, sizeof label, "double-click resets Speed to %.2f Hz", last_value);
    check(label, last_port == PORT_SPEED && fabsf(last_value - 0.5f) < 1e-4f);

    click(w, WX(foot), WY(foot));
    check("footswitch turns the effect off", last_port == PORT_ENGAGE && last_value == 0.0f);
    click(w, WX(foot), WY(foot));
    check("footswitch turns it back on", last_port == PORT_ENGAGE && last_value == 1.0f);

    writes = 0;
    wheel_up(w, WX(output), WY(output));
    snprintf(label, sizeof label, "mouse wheel on Output sends %.2f dB", last_value);
    check(label, writes == 1 && last_port == PORT_OUTPUT && fabsf(last_value - (-6.0f + 0.9f)) < 1e-3f);
    click(w, 5, 5);
    check("clicking empty panel sends nothing new", writes == 1);
}

#endif
