/* fx.c - the flourishes (see fx.h). */
#include "fx.h"
#include "dgk/app.h"
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <math.h>
#include <string.h>

#define FW_PI 3.14159265f
#define DT (1.0f / DGK_TICK_HZ)
#define WALLET_X 600.0f              /* where coins go: the money counter */
#define WALLET_Y 22.0f
#define COIN_TIME 0.9f

static float rnd(fx *f)              /* 0..1, its own sequence (looks only) */
{
    f->rng = f->rng * 1103515245u + 12345u;
    return (float)(f->rng >> 8 & 0xFFFF) / 65535.0f;
}

void fx_init(fx *f, int money)
{
    memset(f, 0, sizeof *f);
    f->money_shown = (float)money;
    f->rng = 2463534242u;
    fx_caps(f, FX_COINS, FX_CONFETTI, FX_DUST);
}

void fx_caps(fx *f, int coins, int confetti, int dust)
{
    f->cap_coins = DGK_CLAMP(coins, 1, FX_COINS);
    f->cap_confetti = DGK_CLAMP(confetti, 1, FX_CONFETTI);
    f->cap_dust = DGK_CLAMP(dust, 1, FX_DUST);
}

/* A slot in a pool: a free one while under the cap, else an old one's. */
static int slot(fx *f, int *used, int cap)
{
    if (*used < cap)
        return (*used)++;
    return (int)(rnd(f) * (cap - 1));
}

void fx_coins(fx *f, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        fx_coin *c = &f->coins[slot(f, &f->ncoins, f->cap_coins)];
        c->delay = i * 0.06f;
        c->t = 0;
    }
    f->max_coins = DGK_MAX(f->max_coins, f->ncoins);
}

void fx_confetti(fx *f, int n)
{
    static const uint32_t palette[6] = { 0xFF5030u, 0xFFD23Fu, 0x5FD05Au, 0x3A9AE8u, 0xC070FFu, 0xFFFFFFu };
    int i;
    for (i = 0; i < n; i++) {
        int left = i & 1;
        fx_bit *b = &f->confetti[slot(f, &f->nconfetti, f->cap_confetti)];
        b->x = left ? 20.0f : 620.0f;                 /* popped from the bottom corners */
        b->y = 470.0f;
        b->vx = (left ? 1.0f : -1.0f) * (60.0f + 220.0f * rnd(f));
        b->vy = -(380.0f + 260.0f * rnd(f));
        b->spin = rnd(f) * 2 * FW_PI;
        b->rate = 6.0f + 10.0f * rnd(f);
        b->life = 2.4f + 0.8f * rnd(f);
        b->rgb = palette[(int)(rnd(f) * 5.99f)];
    }
    f->max_confetti = DGK_MAX(f->max_confetti, f->nconfetti);
}

void fx_dust_at(fx *f, const world *w, float x, float y, int n, float spread)
{
    int i;
    for (i = 0; i < n; i++) {
        fx_dust *d = &f->dust[slot(f, &f->ndust, f->cap_dust)];
        d->x = x + (rnd(f) - 0.5f) * spread;
        d->y = y + (rnd(f) - 0.5f) * spread;
        d->h = world_height(w, d->x, d->y) + 0.3f;
        d->vx = (rnd(f) - 0.5f) * 2.0f;
        d->vy = (rnd(f) - 0.5f) * 2.0f;
        d->size = 0.6f + 0.6f * rnd(f);
        d->life = 0.9f;
    }
    f->max_dust = DGK_MAX(f->max_dust, f->ndust);
}

void fx_bump(fx *f, float lift)
{
    f->sway_v.lift += lift;
}

/* The cab on springs: each angle pulled towards where the accelerations
 * would lean it, a little underdamped so it settles with a wobble. */
static void springs(fx *f, const rig *r)
{
    const float k = 70.0f, c = 7.0f;
    float a_lat = r->v * r->yaw_rate, a_long = (r->v - f->v_prev) / DT;
    float roll_to = DGK_CLAMP(-0.9f * a_lat, -6.0f, 6.0f), pitch_to = DGK_CLAMP(0.5f * a_long, -4.0f, 4.0f);
    f->v_prev = r->v;
    f->sway_v.roll += (k * (roll_to - f->sway.roll) - c * f->sway_v.roll) * DT;
    f->sway_v.pitch += (k * (pitch_to - f->sway.pitch) - c * f->sway_v.pitch) * DT;
    f->sway_v.lift += (k * (0 - f->sway.lift) - c * f->sway_v.lift) * DT;
    f->sway.roll += f->sway_v.roll * DT;
    f->sway.pitch += f->sway_v.pitch * DT;
    f->sway.lift = DGK_CLAMP(f->sway.lift + f->sway_v.lift * DT, -0.15f, 0.25f);
}

void fx_tick(fx *f, const rig *r, const world *w, int money)
{
    int i;
    springs(f, r);
    /* Dust: hard braking at speed, wheelspin from a standstill, every 4 ticks. */
    if ((dgk_app.ticks & 3) == 0) {
        float c = cosf(r->heading), s = sinf(r->heading), ax, ay;
        if (r->brake > 0.6f && fabsf(r->v) > 4.0f) {
            fx_dust_at(f, w, r->x, r->y, 2, 2.0f);
            if (r->has_trailer) {
                rig_trailer_axle(r, &ax, &ay);
                fx_dust_at(f, w, ax, ay, 2, 2.0f);
            }
        } else if (r->throttle > 0.8f && fabsf(r->v) < 2.0f && r->gear == 1)
            fx_dust_at(f, w, r->x - c * 0.5f, r->y - s * 0.5f, 1, 1.5f);
    }
    for (i = 0; i < f->ndust; i++) {                 /* grow, drift, fade; the dead give way */
        fx_dust *d = &f->dust[i];
        d->life -= DT;
        if (d->life <= 0) {
            *d = f->dust[--f->ndust];
            i--;
            continue;
        }
        d->x += d->vx * DT;
        d->y += d->vy * DT;
        d->h += 0.6f * DT;
        d->size += 1.6f * DT;
    }
    for (i = 0; i < f->nconfetti; i++) {
        fx_bit *b = &f->confetti[i];
        b->life -= DT;
        if (b->life <= 0 || b->y > 500.0f) {
            *b = f->confetti[--f->nconfetti];
            i--;
            continue;
        }
        b->vy += 520.0f * DT;                        /* gravity, and air slowing it */
        b->vx *= 0.985f;
        b->vy = DGK_MIN(b->vy, 160.0f);
        b->x += b->vx * DT;
        b->y += b->vy * DT;
        b->spin += b->rate * DT;
    }
    f->coins_landed = 0;
    for (i = 0; i < f->ncoins; i++) {
        fx_coin *c = &f->coins[i];
        if (c->delay > 0) {
            c->delay -= DT;
            continue;
        }
        c->t += DT / COIN_TIME;
        if (c->t >= 1.0f) {
            f->coins_landed++;
            *c = f->coins[--f->ncoins];
            i--;
        }
    }
    /* The wallet counts up (or down) to the money, faster for bigger sums. */
    {
        float d = (float)money - f->money_shown, step = DGK_MAX(2.0f, fabsf(d) * 0.03f);
        f->money_shown = fabsf(d) <= step ? (float)money : f->money_shown + (d > 0 ? step : -step);
    }
}

void fx_draw_world(const fx *f, const camera *cam)
{
    /* Billboards: the camera's right and up on the ground and in height. */
    float rx = sinf(cam->yaw), ry = -cosf(cam->yaw), sp = sinf(cam->pitch), cp = cosf(cam->pitch);
    float ux = cosf(cam->yaw) * sp, uy = sinf(cam->yaw) * sp, uh = cp;   /* right x view: screen up */
    int i;
    if (!f->ndust)
        return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    for (i = 0; i < f->ndust; i++) {
        const fx_dust *d = &f->dust[i];
        float s = d->size * 0.5f;
        glColor4ub(214, 196, 160, (GLubyte)(150.0f * DGK_CLAMP(d->life / 0.9f, 0.0f, 1.0f)));
        glVertex3f(d->x + (-rx - ux) * s, d->h - uh * s, -(d->y + (-ry - uy) * s));
        glVertex3f(d->x + (rx - ux) * s, d->h - uh * s, -(d->y + (ry - uy) * s));
        glVertex3f(d->x + (rx + ux) * s, d->h + uh * s, -(d->y + (ry + uy) * s));
        glVertex3f(d->x + (-rx + ux) * s, d->h + uh * s, -(d->y + (-ry + uy) * s));
    }
    glEnd();
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

static void quad(float x, float y, float hw, float hh, uint32_t rgb)
{
    glColor3ub((GLubyte)(rgb >> 16), (GLubyte)(rgb >> 8), (GLubyte)rgb);
    glVertex2f(x - hw, y - hh);
    glVertex2f(x + hw, y - hh);
    glVertex2f(x + hw, y + hh);
    glVertex2f(x - hw, y + hh);
}

void fx_draw_screen(const fx *f, float alpha)
{
    int i;
    if (!f->ncoins && !f->nconfetti)
        return;
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    for (i = 0; i < f->nconfetti; i++) {             /* each piece flips as it turns: its width */
        const fx_bit *b = &f->confetti[i];
        float flip = fabsf(cosf(b->spin + b->rate * alpha * DT));
        quad(b->x + b->vx * alpha * DT, b->y + b->vy * alpha * DT, 1.5f + 6.5f * flip, 5.0f, b->rgb);
    }
    for (i = 0; i < f->ncoins; i++) {                /* a coin: an arc from the callout up to the wallet */
        const fx_coin *c = &f->coins[i];
        float t, e, x, y, spin;
        if (c->delay > 0)
            continue;
        t = DGK_MIN(1.0f, c->t + alpha * DT / COIN_TIME);
        e = t * t * (3 - 2 * t);
        x = 320.0f + (WALLET_X - 320.0f) * e + 60.0f * sinf(t * FW_PI) * ((i & 1) ? 1.0f : -1.0f);
        y = 230.0f + (WALLET_Y - 230.0f) * e - 90.0f * sinf(t * FW_PI);
        spin = fabsf(cosf(t * 9.0f + i));
        quad(x, y, 2.0f + 7.0f * spin, 9.0f, 0x7A5A10u);    /* the rim, then the face */
        quad(x, y, 1.0f + 5.5f * spin, 7.5f, 0xFFD23Fu);
    }
    glEnd();
}
