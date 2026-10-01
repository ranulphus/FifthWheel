/* fx.h - the flourishes (looks only; never fed back into the simulation):
 * coins flying from a callout to the wallet while the money counts up,
 * confetti popping from the screen's bottom corners, dust puffs on the
 * ground, and the cab rocking on its springs. Each lives in a fixed pool:
 * at most FX_COINS, FX_CONFETTI and FX_DUST at once (a new one takes the
 * oldest's place), so a frame's cost has a ceiling. */
#ifndef FW_FX_H
#define FW_FX_H

#include "camera.h"
#include "lorry.h"

#define FX_COINS 16
#define FX_CONFETTI 64
#define FX_DUST 32

typedef struct fx_coin { float delay, t; } fx_coin;
typedef struct fx_bit { float x, y, vx, vy, spin, rate, life; uint32_t rgb; } fx_bit;
typedef struct fx_dust { float x, y, h, vx, vy, size, life; } fx_dust;

typedef struct fx {
    fx_coin coins[FX_COINS];
    fx_bit confetti[FX_CONFETTI];
    fx_dust dust[FX_DUST];
    int ncoins, nconfetti, ndust;    /* in use (the most since the start: max_*) */
    int max_coins, max_confetti, max_dust;
    float money_shown;               /* the wallet as drawn: counts up to the money */
    int coins_landed;                /* this tick (a coin sound each) */
    cab_sway sway, sway_v;           /* the cab's springs: angles and their rates */
    float v_prev;
    uint32_t rng;
} fx;

void fx_init(fx *f, int money);
/* After each tick: the springs from the rig's accelerations, dust from
 * its brakes, wheelspin and knocks; everything moves on. */
void fx_tick(fx *f, const rig *r, const world *w, int money);
void fx_coins(fx *f, int n);         /* a delivery's pay flying to the wallet */
void fx_confetti(fx *f, int n);
void fx_dust_at(fx *f, const world *w, float x, float y, int n, float spread);
void fx_bump(fx *f, float lift);     /* a jolt to the cab's springs */
/* Dust in the world (after the world and the lorry), facing the camera. */
void fx_draw_world(const fx *f, const camera *cam);
/* Coins and confetti, in the 640x480 overlay. */
void fx_draw_screen(const fx *f, float alpha);

#endif
