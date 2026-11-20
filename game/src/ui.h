/* ui.h - the HUD's and menus' shapes, in the 640x480 overlay, cartoon-style:
 * rounded panels with a soft shadow and a light rim, rounded bars, a round
 * dial with an arc and a needle, a badge, a gold coin. Immediate mode; each
 * shape is one or two draws. Colours are 0xRRGGBBAA. */
#ifndef FW_UI_H
#define FW_UI_H

#include "dgk/base.h"

#define UI_PANEL 0x1C2E5AC8u          /* a panel's deep blue, translucent */
#define UI_RIM 0xFFFFFF70u            /* its rim */
#define UI_GOLD 0xFFD23FFFu

/* A panel: a rounded rectangle (corner radius r) in `fill`, a 2-pixel rim
 * in `rim` (0: none) and a soft shadow below and to the right. */
void ui_panel(float x, float y, float w, float h, float r, uint32_t fill, uint32_t rim);
/* A rounded bar: its track, then `frac` (0..1) of it in `rgba`. */
void ui_bar(float x, float y, float w, float h, float frac, uint32_t rgba);
/* A disc of radius r at (x, y), and a ring from r0 to r1, n segments. */
void ui_disc(float x, float y, float r, uint32_t rgba, int n);
void ui_ring(float x, float y, float r0, float r1, uint32_t rgba, int n);
/* An arc of the ring from angle a0 to a1 (radians, anticlockwise from
 * screen right, screen y down: so a positive angle is up). */
void ui_arc(float x, float y, float r0, float r1, float a0, float a1, uint32_t rgba, int n);
/* A needle from (x, y) at angle a, `len` long. */
void ui_needle(float x, float y, float a, float len, uint32_t rgba);
/* A gold coin of radius r at (x, y). */
void ui_coin(float x, float y, float r);

#endif
