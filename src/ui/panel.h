/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * House style from style.h, font.h and canvas.h. The design is original: it uses its own
 * name, lettering and colours, and none of the original pedal's branding or artwork. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "../ports.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 600
#define PANEL_H 390

#define PANEL_NAME "TANGERINE ORBIT"
#define PANEL_SUB  "FOUR-STAGE JFET PHASE SHIFTER"

/* Readout window */
#define DISP_X0 332
#define DISP_X1 584

enum { W_KNOB, W_TRIM, W_FOOT };

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def;
    int log;
    const char* label; /* on the panel */
    const char* name;  /* in the display */
    const char* lo;    /* scale end labels */
    const char* hi;
} Widget;

static const Widget WIDGETS[] = {
    { W_KNOB, PORT_SPEED, 118, 152, 34, 0.2f, 10.0f, 0.5f, 1, "SPEED", "SPEED", "0.2", "10" },
    { W_TRIM, PORT_BIAS, 375, 146, 17, 0.0f, 1.0f, 0.46f, 0, "BIAS", "BIAS TRIM", 0, 0 },
    { W_TRIM, PORT_VOLTS, 484, 146, 17, 0.1f, 10.0f, 1.0f, 1, "INPUT V", "VOLTS AT 0 DBFS", 0, 0 },
    { W_TRIM, PORT_OUTPUT, 552, 146, 17, -24.0f, 12.0f, -6.0f, 0, "OUTPUT", "OUTPUT LEVEL", 0, 0 },
    { W_FOOT, PORT_ENGAGE, 300, 314, 25, 0.0f, 1.0f, 1.0f, 0, "EFFECT", "EFFECT", 0, 0 },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

/* Sweep lamp: follows the oscillator, read from the plugin's sweep output port. */
#define LAMP_X 238.0f
#define LAMP_Y 146.0f

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */

typedef void (*PanelWrite)(void* host, int port, float value);
/* Optional: brackets each user edit (begin = 1, then writes, then begin = 0), so hosts
 * that record automation see one gesture per drag. */
typedef void (*PanelGesture)(void* host, int port, int begin);
/* Optional: called by the window's timer, so a wrapper can feed in live values such as
 * the sweep lamp when its host has no port events. */
typedef void (*PanelPoll)(void* host);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[PORT_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    PanelGesture gesture; /* may be NULL; set after panel_init */
    PanelPoll poll;       /* may be NULL; set after panel_init */
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    float n = w->log ? logf(v / w->min) / logf(w->max / w->min) : (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->log ? w->min * powf(w->max / w->min, n) : w->min + n * (w->max - w->min);
}

static int w_on(const Panel* p, const Widget* w) { return p->val[w->port] > 0.5f; }

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->port];
    switch (w->port) {
    case PORT_SPEED:  snprintf(buf, n, "%.2f HZ", v); break;
    case PORT_VOLTS:  snprintf(buf, n, "%.2f V", v); break;
    case PORT_OUTPUT: snprintf(buf, n, "%+.1f DB", v); break;
    case PORT_ENGAGE: snprintf(buf, n, "%s", v > 0.5f ? "ON" : "BYPASS"); break;
    default:          snprintf(buf, n, "%.2f", v); break;
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, TXT_SECTION, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, TXT_SECTION, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + (big ? 22.0f : 16.0f);
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? TXT_LABEL : 8.0f, 2.0f,
                big ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_foot_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 9, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 8, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 8, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 18, w->label, TXT_LABEL, 2.5f, 1.7f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 26, 7.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 26, 7.5f, 1.5f, 0x8a8e96, 0.8f);
}

static void draw_lamp_static(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 2, 11, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 11, 0xb9bcc3, 0x45484e, -0.4f, -0.6f, 1);
    cv_circle(c, x, y, 7.5f, 0x140c04, 1);
    text_center(c, x, y + 18, "SWEEP", TXT_SMALL, 2.0f, 1.2f, COL_DIM, 1);
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, "DAVIDIAN " PANEL_NAME, 14.5f, 1.6f, 2.2f, COL_INK, 1);
    text_draw(&c, 32, 46, PANEL_SUB, TXT_SMALL, 2.4f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, 312, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, DISP_X0, 14, DISP_X1, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, DISP_X0 + 2, 16, DISP_X1 - 2, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, DISP_X0, 14, DISP_X1, 56, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, 76, 300, 236, "SWEEP");
    draw_section(&c, 310, 76, 440, 236, "CALIBRATE");
    draw_section(&c, 450, 76, 584, 236, "LEVELS");

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        if (w->kind == W_FOOT)
            draw_foot_static(&c, w);
        else
            draw_knob_scale(&c, w);
    }
    draw_lamp_static(&c, LAMP_X, LAMP_Y);

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int knurls = w->kind == W_KNOB ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), w->kind == W_KNOB ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), w->kind == W_KNOB ? 3.0f : 2.2f, COL_INK, 1);
}

static void draw_foot(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 26;
    cv_shaded_circle(c, w->x, w->y, w->r, 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 5, 16, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 5.5f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 5.5f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static void draw_lamp(Canvas* c, float x, float y, float b)
{
    b = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
    cv_circle(c, x, y, 7.5f, mix(0x1e1206, COL_AMBER, b), 1);
    if (b > 0.02f) {
        cv_glow(c, x, y, 4, 20, COL_AMBER, 0.45f * b);
        cv_circle(c, x - 2, y - 2, 2.2f, 0xfff2d6, 0.8f * b);
    }
}

static void draw_display(Panel* p)
{
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, DISP_X0 + 16, 22, PANEL_NAME, 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, DISP_X0 + 16, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS", 7, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    text_draw(c, DISP_X0 + 16, 22, WIDGETS[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, DISP_X0 + 16, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        if (w->kind == W_FOOT)
            draw_foot(&p->c, w, w_on(p, w));
        else
            draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i);
    }
    /* The lamp only lights while the effect is in, like the footswitch LED. */
    draw_lamp(&p->c, LAMP_X, LAMP_Y,
              p->val[PORT_ENGAGE] > 0.5f && p->val[PORT_ENABLED] > 0.5f ? p->val[PORT_SWEEP] : 0.0f);
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i)
        p->val[WIDGETS[i].port] = WIDGETS[i].def;
    p->val[PORT_ENABLED] = 1.0f;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= PORT_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    if (w->kind == W_FOOT)
        v = floorf(v + 0.5f);
    if (v < w->min) v = w->min;
    if (v > w->max) v = w->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        /* Outside a drag (clicks, wheel, double-click) each change is its own gesture. */
        const int lone = p->drag != wi && p->gesture;
        p->val[w->port] = v;
        if (lone)
            p->gesture(p->host, w->port, 1);
        if (p->write)
            p->write(p->host, w->port, v);
        if (lone)
            p->gesture(p->host, w->port, 0);
    }
}

static void drag_start(Panel* p, int wi)
{
    p->drag = wi;
    if (p->gesture)
        p->gesture(p->host, WIDGETS[wi].port, 1);
}

static int hit(float x, float y)
{
    int i;
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
            return i;
    }
    return -1;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_FOOT) {
        set_value(p, i, w_on(p, w) ? 0.0f : 1.0f);
        return;
    }
    if (dbl) {
        set_value(p, i, w->def);
        return;
    }
    drag_start(p, i);
    p->drag_y = y;
    p->drag_norm = w_norm(w, p->val[w->port]);
    p->shown = i;
    p->dirty = 1;
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    (void)x;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    {
        const float n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
        /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->drag_y = y;
    }
}

static void panel_mouse_up(Panel* p)
{
    const int d = p->drag;
    if (d < 0)
        return;
    p->dirty = 1;
    p->drag = -1;
    if (p->gesture)
        p->gesture(p->host, WIDGETS[d].port, 0);
}

/* The window's timer calls this before redrawing. */
static void panel_poll(Panel* p)
{
    if (p->poll)
        p->poll(p->host);
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind != W_FOOT)
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
}

#endif
