/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* House style for all LV2 plugin GUIs: palette and lettering sizes.
 * Copy src/ui/canvas.h, font.h and this file into a new project to get the same look:
 * graphite powder-coat panel, cream stroke lettering, amber accents, red footswitch LEDs.
 * Colours are 0xRRGGBB. */
#ifndef UI_STYLE_H
#define UI_STYLE_H

/* Panel */
#define COL_PANEL_TOP 0x2c2f35 /* vertical gradient, top */
#define COL_PANEL_BOT 0x1b1d21 /* vertical gradient, bottom */
#define PANEL_TEXTURE 0.07f    /* strength of cv_texture() noise */
#define COL_LINE      0x5d626c /* section outlines */

/* Lettering (font.h stroke font) */
#define COL_INK       0xe9e1cf /* cream: titles, control labels, knob pointers */
#define COL_DIM       0x8c8f96 /* grey: scale ends, inactive switch positions */
#define COL_AMBER     0xffb547 /* accent: section titles, value arcs, active positions, readout */
#define COL_AMBER_DK  0x5a3a10 /* readout idle text */
#define COL_RED       0xff3b2f /* footswitch LEDs */

/* Metal parts: shaded from light to dark, lit from the upper left */
#define COL_METAL_LIGHT 0xd0d3d9
#define COL_METAL_DARK  0x575a61
#define COL_KNOB_LIGHT  0x4a4c52 /* black knob body */
#define COL_KNOB_DARK   0x101012

/* Text sizes (cap height in px) and letter spacing (font grid units) */
#define TXT_TITLE   20.0f /* product name, track 3.2, weight 2.8 */
#define TXT_SECTION  9.0f /* section titles, track 2.5, amber */
#define TXT_LABEL   10.0f /* main knob labels, track 2.0 */
#define TXT_SMALL    7.0f /* scale ends, switch positions */

#endif
