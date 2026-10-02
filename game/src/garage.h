/* garage.h - the garage (G, stopped): a showroom where the tractor turns
 * on a platform under the sky, and the shop. Left and right browse a
 * category's items, up and down change category, Enter buys the item shown
 * (and fits it) or fits one already owned, Esc leaves. What is shown is
 * tried on: paint, decals and the cab on the tractor, a licence's trailer
 * behind it, a horn sounded. Each step logs an FW-MENU line. */
#ifndef FW_GARAGE_H
#define FW_GARAGE_H

#include "career.h"
#include "dgk/text.h"
#include "lorry.h"

typedef struct garage {
    int open;
    int kind;                       /* the category shown */
    int shown[KINDS];               /* the item shown in each (index into items[]) */
    float spin, spin_prev;          /* the turntable, radians */
    const char *message;            /* a flash: BOUGHT!, NOT ENOUGH MONEY ... */
    uint32_t message_rgba, message_tick;
} garage;

void garage_open(garage *g, const career *c, lorry_meshes *m);
/* One tick. Returns 1 if the career changed (bought or fitted: save it),
 * and sets g->open to 0 when the player leaves (the caller restyles the
 * lorry from the career then). */
int  garage_tick(garage *g, career *c, lorry_meshes *m);
void garage_draw(const garage *g, const career *c, const lorry_meshes *m, const dgk_font *font, float alpha);
/* The showroom alone: a sky, a platform with a coloured rim, the tractor
 * turning (spin, radians) with a trailer of that type behind (-1: none). */
void showroom_draw(const lorry_meshes *m, float spin, uint32_t rim_rgba, int trailer);

#endif
