/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* An original stroke font for panel lettering. Each glyph is a set of strokes on a grid
 * 6 units wide and 12 high (y down). A stroke is a run of two-character points, x then y,
 * each a base-16 digit; strokes are separated by spaces. Drawn with round-ended lines,
 * it reads like engraved or screen-printed panel text at any size. */
#ifndef FONT_H
#define FONT_H

#include <stdio.h>

#include "canvas.h"

static const char* glyph(char ch)
{
    switch (ch >= 'a' && ch <= 'z' ? ch - 32 : ch) {
    case 'A': return "0c306c 1858";
    case 'B': return "0c004051554606 46575b4c0c";
    case 'C': return "615010010b1c5c6b";
    case 'D': return "0c0040626a4c0c";
    case 'E': return "60000c6c 0646";
    case 'F': return "60000c 0646";
    case 'G': return "615010010b1c5c6b6737";
    case 'H': return "000c 606c 0666";
    case 'I': return "1050 303c 1c5c";
    case 'J': return "606b5c1c0b09";
    case 'K': return "000c 6006 246c";
    case 'L': return "000c6c";
    case 'M': return "0c0036606c";
    case 'N': return "0c006c60";
    case 'O': return "1050616b5c1c0b0110";
    case 'P': return "0c005061655606";
    case 'Q': return "1050616b5c1c0b0110 486c";
    case 'R': return "0c005061655606 366c";
    case 'S': return "61501001051656676b5c1c0b";
    case 'T': return "0060 303c";
    case 'U': return "000b1c5c6b60";
    case 'V': return "003c60";
    case 'W': return "001c365c60";
    case 'X': return "006c 600c";
    case 'Y': return "003660 363c";
    case 'Z': return "00600c6c";
    case '0': return "1050616b5c1c0b0110";
    case '1': return "12303c 1c5c";
    case '2': return "01105061650c6c";
    case '3': return "01105061655626 56676b5c1c0b";
    case '4': return "5c500868";
    case '5': return "60000555666b5c1c0b";
    case '6': return "5020030b1c5c6b675606";
    case '7': return "00602c";
    case '8': return "105061655616050110 16070b1c5c6b6756";
    case '9': return "66160501105061693c1c";
    case '.': return "2b2c";
    case ',': return "2b2d";
    case ':': return "2323 2b2b";
    case '-': return "1646";
    case '+': return "1646 3339";
    case '/': return "0c60";
    case '%': return "0c60 0001 5b5c";
    case '(': return "40220a4c";
    case ')': return "20424a2c";
    case '\'': return "3032";
    default: return "";
    }
}

/* Advance in grid units. */
static float glyph_advance(char ch)
{
    switch (ch) {
    case ' ': return 5.0f;
    case '.': case ',': case ':': case '\'': return 4.0f;
    case '1': case 'I': case 'i': return 7.0f;
    default: return 8.5f;
    }
}

/* Text width in pixels for cap height `size` and letter spacing `track` (grid units). */
static float text_width(const char* s, float size, float track)
{
    const float u = size / 12.0f;
    float w = 0.0f;
    int n = 0;
    for (; *s; ++s, ++n)
        w += (glyph_advance(*s) + track) * u;
    return n ? w - (2.5f + track) * u : 0.0f;
}

static int hexv(char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; }

/* Draws text with its top-left at (x, y). Returns the width. */
static float text_draw(Canvas* c, float x, float y, const char* s, float size, float track,
                       float weight, uint32_t col, float a)
{
    const float u = size / 12.0f;
    const float x_start = x;
    for (; *s; ++s) {
        const char* g = glyph(*s);
        float ox = x;
        if (*s == '1' || *s == 'I' || *s == 'i')
            ox -= 1.0f * u;
        while (*g) {
            float px = 0.0f, py = 0.0f;
            int first = 1;
            while (*g && *g != ' ') {
                float nx = ox + hexv(g[0]) * u, ny = y + hexv(g[1]) * u;
                if (!first)
                    cv_line(c, px, py, nx, ny, weight, col, a);
                else if (g[2] == ' ' || g[2] == 0)
                    cv_circle(c, nx, ny, 0.5f * weight, col, a);
                px = nx;
                py = ny;
                first = 0;
                g += 2;
            }
            if (*g == ' ')
                ++g;
        }
        x += (glyph_advance(*s) + track) * u;
    }
    return x - x_start;
}

/* Centred on cx. */
static void text_center(Canvas* c, float cx, float y, const char* s, float size, float track,
                        float weight, uint32_t col, float a)
{
    text_draw(c, cx - 0.5f * text_width(s, size, track), y, s, size, track, weight, col, a);
}

#endif
