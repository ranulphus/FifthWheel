/* ui.h - the HUD's and menus' shapes, in the 640x480 overlay, cartoon-style:
 * rounded panels with a soft shadow and a light rim, rounded bars, a round
 * dial with an arc and a needle, a gold badge, a gold coin. The round parts
 * come from one texture drawn at start-up (ui_init), antialiased: a dial or
 * a coin is one quad, a panel's corners are quarters of a disc, so the HUD
 * is about 90 triangles (the triangles cost the CPU what the world's do).
 * Every shape counts its triangles in dgk_gfx_tris. Colours are 0xRRGGBBAA. */
#ifndef FW_UI_H
#define FW_UI_H

#include "dgk/base.h"

#define UI_PANEL 0x1C2E5AC8u          /* a panel's deep blue, translucent */
#define UI_RIM 0xFFFFFF70u            /* its rim */
#define UI_GOLD 0xFFD23FFFu

/* The speedometer's face, as ui_dial draws it: radius, and its scale's ends
 * (degrees anticlockwise from screen right). */
#define UI_DIAL_R 54.0f
#define UI_DIAL_A0 210.0f
#define UI_DIAL_A1 -30.0f
#define UI_MAP_R 62.0f                /* the minimap's disc, inside its rim */

/* Draws the texture; once the GL context is up. Returns 0. */
int ui_init(void);

/* A panel: a rounded rectangle (corner radius r) in `fill`, a 2-pixel rim
 * in `rim` (0: none) and a soft shadow below and to the right. A plate is a
 * panel without the shadow (the HUD's, over the world). With r half the
 * height they are pills. */
void ui_panel(float x, float y, float w, float h, float r, uint32_t fill, uint32_t rim);
void ui_plate(float x, float y, float w, float h, float r, uint32_t fill, uint32_t rim);
/* A rounded bar: its track, then `frac` (0..1) of it in `rgba`. */
void ui_bar(float x, float y, float w, float h, float frac, uint32_t rgba);
/* A disc of radius r at (x, y). */
void ui_disc(float x, float y, float r, uint32_t rgba);
/* An arc of the ring from r0 to r1, from angle a0 to a1 (radians,
 * anticlockwise from screen right, screen y down: so a positive angle is
 * up), n segments. */
void ui_arc(float x, float y, float r0, float r1, float a0, float a1, uint32_t rgba, int n);
/* A needle from (x, y) at angle a, `len` long. */
void ui_needle(float x, float y, float a, float len, uint32_t rgba);
/* A gold coin of radius r at (x, y). */
void ui_coin(float x, float y, float r);
/* The speedometer's face at (x, y): its shadow, a white rim, the deep blue
 * face, a tick every fifth of the scale and the revs' track (UI_DIAL_*). */
void ui_dial(float x, float y);
/* The gear's badge: a gold medallion, radius 18, rimmed in white. */
void ui_medal(float x, float y);
/* The minimap's translucent disc (UI_MAP_R) in a white rim and a deep blue
 * ring, 6 pixels in all. */
void ui_map_frame(float x, float y);

#endif
