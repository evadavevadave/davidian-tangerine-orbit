/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Renders the GUI panel to a .bmp without opening a window, on any platform.
 * Usage: ui_render <out.bmp> */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include "../src/ui/panel.h"

static int write_bmp(const char* path, const uint32_t* px, int w, int h)
{
    unsigned char hdr[54] = { 'B', 'M' };
    const unsigned size = 54u + 4u * (unsigned)(w * h);
    FILE* f = fopen(path, "wb");
    int i;
    if (!f)
        return 0;
    for (i = 0; i < 4; ++i) {
        hdr[2 + i] = (unsigned char)(size >> (8 * i));
        hdr[18 + i] = (unsigned char)((unsigned)w >> (8 * i));
        hdr[22 + i] = (unsigned char)((unsigned)(-h) >> (8 * i)); /* negative: top-down */
    }
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[26] = 1;
    hdr[28] = 32;
    fwrite(hdr, 1, 54, f);
    fwrite(px, 4, (size_t)(w * h), f); /* 0x00RRGGBB is BGRA in memory on little-endian */
    fclose(f);
    return 1;
}

int main(int argc, char** argv)
{
    Panel p;
    if (argc < 2) {
        printf("usage: ui_render <out.bmp>\n");
        return 2;
    }
    if (!panel_init(&p, NULL, NULL))
        return 1;
    /* A mid-sweep moment with the speed being adjusted. */
    panel_set(&p, PORT_SPEED, 1.2f);
    panel_set(&p, PORT_SWEEP, 0.7f);
    p.shown = 0; /* SPEED */
    panel_draw(&p);
    if (!write_bmp(argv[1], p.px, PANEL_W, PANEL_H)) {
        printf("cannot write %s\n", argv[1]);
        return 1;
    }
    printf("wrote %s (%dx%d)\n", argv[1], PANEL_W, PANEL_H);
    panel_free(&p);
    return 0;
}
