/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* A small anti-aliased software renderer, so the GUI needs no graphics library.
 * Pixels are 0x00RRGGBB, which matches a 32-bit Windows DIB and a 24-bit TrueColor X11
 * visual. Every shape is drawn from its signed distance, which gives clean edges at any size. */
#ifndef CANVAS_H
#define CANVAS_H

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t* px;
    int w, h;
    /* Clip rectangle, inclusive-exclusive. */
    int cx0, cy0, cx1, cy1;
} Canvas;

#define CV_PI 3.14159265358979f

static inline uint32_t rgb(int r, int g, int b)
{
    return ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}

static inline uint32_t mix(uint32_t a, uint32_t b, float t)
{
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return rgb(ar + (int)((br - ar) * t + 0.5f), ag + (int)((bg - ag) * t + 0.5f),
               ab + (int)((bb - ab) * t + 0.5f));
}

static inline void cv_init(Canvas* c, uint32_t* px, int w, int h)
{
    c->px = px;
    c->w = w;
    c->h = h;
    c->cx0 = c->cy0 = 0;
    c->cx1 = w;
    c->cy1 = h;
}

static inline void cv_blend(Canvas* c, int x, int y, uint32_t col, float a)
{
    uint32_t* p;
    if (a <= 0.003f || x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1)
        return;
    p = &c->px[y * c->w + x];
    *p = a >= 0.997f ? col : mix(*p, col, a);
}

/* Coverage of a pixel whose centre is d pixels outside an edge. */
static inline float cv_cov(float d)
{
    return d <= -0.5f ? 1.0f : (d >= 0.5f ? 0.0f : 0.5f - d);
}

static inline int cv_lo(float v) { return (int)floorf(v); }
static inline int cv_hi(float v) { return (int)ceilf(v) + 1; }

static void cv_fill(Canvas* c, uint32_t col)
{
    int i, n = c->w * c->h;
    for (i = 0; i < n; ++i)
        c->px[i] = col;
}

static void cv_circle(Canvas* c, float cx, float cy, float r, uint32_t col, float a)
{
    int x, y, x0 = cv_lo(cx - r - 1), x1 = cv_hi(cx + r + 1), y0 = cv_lo(cy - r - 1), y1 = cv_hi(cy + r + 1);
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            cv_blend(c, x, y, col, a * cv_cov(sqrtf(dx * dx + dy * dy) - r));
        }
}

/* Circle shaded from col_light at the point (lx, ly), given relative to the radius, to
 * col_dark at the far edge. */
static void cv_shaded_circle(Canvas* c, float cx, float cy, float r, uint32_t col_light,
                             uint32_t col_dark, float lx, float ly, float a)
{
    int x, y, x0 = cv_lo(cx - r - 1), x1 = cv_hi(cx + r + 1), y0 = cv_lo(cy - r - 1), y1 = cv_hi(cy + r + 1);
    const float px = cx + lx * r, py = cy + ly * r;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float k = cv_cov(sqrtf(dx * dx + dy * dy) - r);
            if (k > 0.0f) {
                float ex = x + 0.5f - px, ey = y + 0.5f - py;
                float t = sqrtf(ex * ex + ey * ey) / (1.8f * r);
                cv_blend(c, x, y, mix(col_light, col_dark, t), a * k);
            }
        }
}

/* Soft glow: alpha falls off with distance beyond the core radius. */
static void cv_glow(Canvas* c, float cx, float cy, float r, float spread, uint32_t col, float a)
{
    const float R = r + spread;
    int x, y, x0 = cv_lo(cx - R), x1 = cv_hi(cx + R), y0 = cv_lo(cy - R), y1 = cv_hi(cy + R);
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy) - r;
            float t = d <= 0.0f ? 1.0f : 1.0f - d / spread;
            if (t > 0.0f)
                cv_blend(c, x, y, col, a * t * t);
        }
}

static void cv_ring(Canvas* c, float cx, float cy, float r, float w, uint32_t col, float a)
{
    const float R = r + w;
    int x, y, x0 = cv_lo(cx - R), x1 = cv_hi(cx + R), y0 = cv_lo(cy - R), y1 = cv_hi(cy + R);
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            cv_blend(c, x, y, col, a * cv_cov(fabsf(sqrtf(dx * dx + dy * dy) - r) - 0.5f * w));
        }
}

/* Thick line with round ends. */
static void cv_line(Canvas* c, float ax, float ay, float bx, float by, float w, uint32_t col, float a)
{
    const float r = 0.5f * w;
    const float vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    int x, y;
    int x0 = cv_lo(fminf(ax, bx) - r - 1), x1 = cv_hi(fmaxf(ax, bx) + r + 1);
    int y0 = cv_lo(fminf(ay, by) - r - 1), y1 = cv_hi(fmaxf(ay, by) + r + 1);
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = len2 > 0.0f ? (px * vx + py * vy) / len2 : 0.0f;
            float dx, dy;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            dx = px - t * vx;
            dy = py - t * vy;
            cv_blend(c, x, y, col, a * cv_cov(sqrtf(dx * dx + dy * dy) - r));
        }
}

/* Angles are radians clockwise from straight up. */
static inline float cv_ax(float cx, float r, float ang) { return cx + r * sinf(ang); }
static inline float cv_ay(float cy, float r, float ang) { return cy - r * cosf(ang); }

static void cv_arc(Canvas* c, float cx, float cy, float r, float w, float a0, float a1,
                   uint32_t col, float a)
{
    const float R = r + w;
    int x, y, x0 = cv_lo(cx - R), x1 = cv_hi(cx + R), y0 = cv_lo(cy - R), y1 = cv_hi(cy + R);
    if (a1 < a0) { float t = a0; a0 = a1; a1 = t; }
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float ang = atan2f(dx, -dy);
            if (ang >= a0 && ang <= a1)
                cv_blend(c, x, y, col, a * cv_cov(fabsf(sqrtf(dx * dx + dy * dy) - r) - 0.5f * w));
        }
    /* Round caps also smooth the angular ends. */
    cv_circle(c, cv_ax(cx, r, a0), cv_ay(cy, r, a0), 0.5f * w, col, a);
    cv_circle(c, cv_ax(cx, r, a1), cv_ay(cy, r, a1), 0.5f * w, col, a);
}

static float cv_rrect_dist(float px, float py, float x0, float y0, float x1, float y1, float rad)
{
    const float hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0);
    float qx = fabsf(px - 0.5f * (x0 + x1)) - hx + rad;
    float qy = fabsf(py - 0.5f * (y0 + y1)) - hy + rad;
    float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0.0f ? in : 0.0f) - rad;
}

static void cv_rrect(Canvas* c, float x0, float y0, float x1, float y1, float rad, uint32_t col, float a)
{
    int x, y;
    for (y = cv_lo(y0 - 1); y < cv_hi(y1 + 1); ++y)
        for (x = cv_lo(x0 - 1); x < cv_hi(x1 + 1); ++x)
            cv_blend(c, x, y, col, a * cv_cov(cv_rrect_dist(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad)));
}

static void cv_rrect_line(Canvas* c, float x0, float y0, float x1, float y1, float rad, float w,
                          uint32_t col, float a)
{
    int x, y;
    for (y = cv_lo(y0 - w - 1); y < cv_hi(y1 + w + 1); ++y)
        for (x = cv_lo(x0 - w - 1); x < cv_hi(x1 + w + 1); ++x) {
            float d = cv_rrect_dist(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad);
            cv_blend(c, x, y, col, a * cv_cov(fabsf(d) - 0.5f * w));
        }
}

/* Rounded rectangle with a vertical gradient. */
static void cv_rrect_vgrad(Canvas* c, float x0, float y0, float x1, float y1, float rad,
                           uint32_t top, uint32_t bottom, float a)
{
    int x, y;
    for (y = cv_lo(y0 - 1); y < cv_hi(y1 + 1); ++y) {
        uint32_t col = mix(top, bottom, (y + 0.5f - y0) / (y1 - y0));
        for (x = cv_lo(x0 - 1); x < cv_hi(x1 + 1); ++x)
            cv_blend(c, x, y, col, a * cv_cov(cv_rrect_dist(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad)));
    }
}

/* Deterministic per-pixel noise, for a fine powder-coat texture. */
static inline float cv_noise(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 1023) / 1023.0f;
}

static void cv_texture(Canvas* c, float strength)
{
    int x, y;
    for (y = 0; y < c->h; ++y)
        for (x = 0; x < c->w; ++x) {
            float n = cv_noise(x, y) - 0.5f;
            uint32_t* p = &c->px[y * c->w + x];
            *p = n > 0.0f ? mix(*p, 0xffffff, n * strength) : mix(*p, 0x000000, -n * strength);
        }
}

#endif
