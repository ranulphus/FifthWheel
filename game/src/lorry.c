/* lorry.c - the toy lorry: a stubby cab in the player's paint (and decal,
 * and the big cab if bought) on a chassis with big wheels; a tall box
 * trailer, a flatbed with crates, a tanker. */
#include "lorry.h"
#include <GL/gl.h>
#include <math.h>

#define TV 512
#define TT 512
static float tp[TV * 3], cp[TV * 3], rp[TRAILER_TYPES][TV * 3];
static uint8_t tc[TV * 4], cc[TV * 4], rc[TRAILER_TYPES][TV * 4];
static uint16_t ti[TT * 3], ci[TT * 3], ri[TRAILER_TYPES][TT * 3];

/* A wheel: a dark tyre block with a bright hub, x along the lorry. */
static void wheel(builder *b, float x, float side)
{
    float z0 = side < 0 ? -1.55f : 1.05f;
    mb_box(b, x - 0.65f, 0.0f, z0, x + 0.65f, 1.3f, z0 + 0.5f, 0x26262Cu);
    mb_box(b, x - 0.25f, 0.45f, side < 0 ? z0 - 0.05f : z0 + 0.5f, x + 0.25f, 0.85f, side < 0 ? z0 : z0 + 0.55f,
           0xD0D4DCu);
}

/* An octagonal tank along x, its ends in their own colour. */
static void tank(builder *b, float x0, float x1, float cy, float r, uint32_t rgb, uint32_t ends)
{
    float ring[8][2], p[4][3], q[4][3];
    int i, k;
    for (i = 0; i < 8; i++) {
        float a = (i + 0.5f) * 3.14159265f / 4;
        ring[i][0] = cy + r * cosf(a);
        ring[i][1] = r * sinf(a);
    }
    for (i = 0; i < 8; i++) {
        int n = (i + 1) & 7;
        float side[4][3] = { { x0, ring[i][0], ring[i][1] }, { x0, ring[n][0], ring[n][1] },
                             { x1, ring[n][0], ring[n][1] }, { x1, ring[i][0], ring[i][1] } };
        mb_quad(b, side[0], side[1], side[2], side[3], rgb);
    }
    {
        static const int caps[3][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 3, 4, 7 } };
        for (i = 0; i < 3; i++) {
            for (k = 0; k < 4; k++) {
                int f = caps[i][k], bk = caps[i][3 - k];
                p[k][0] = x1; p[k][1] = ring[f][0]; p[k][2] = ring[f][1];         /* front, facing +x */
                q[k][0] = x0; q[k][1] = ring[bk][0]; q[k][2] = ring[bk][1];       /* back, facing -x */
            }
            mb_quad(b, p[0], p[1], p[2], p[3], ends);
            mb_quad(b, q[0], q[1], q[2], q[3], ends);
        }
    }
}

/* A shape on both sides of the cab: points (x, y) anticlockwise as seen
 * from +z, at z = +-side, each side facing out; a fan from the first. */
static void decal(builder *b, const float (*pt)[2], int n, float side, uint32_t rgb)
{
    int i, k;
    for (i = 1; i + 1 < n; i++) {
        const int f[3] = { 0, i, i + 1 };
        float l[4][3], r[4][3];
        for (k = 0; k < 3; k++) {
            l[k][0] = pt[f[k]][0]; l[k][1] = pt[f[k]][1]; l[k][2] = side;
            r[2 - k][0] = pt[f[k]][0]; r[2 - k][1] = pt[f[k]][1]; r[2 - k][2] = -side;
        }
        l[3][0] = l[2][0]; l[3][1] = l[2][1]; l[3][2] = l[2][2];     /* a triangle: the last point twice */
        r[3][0] = r[2][0]; r[3][1] = r[2][1]; r[3][2] = r[2][2];
        mb_quad(b, l[0], l[1], l[2], l[3], rgb);
        mb_quad(b, r[0], r[1], r[2], r[3], rgb);
    }
}

static void quad2(builder *b, float x0, float y0, float x1, float y1, float side, uint32_t rgb)
{
    const float pt[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    decal(b, pt, 4, side, rgb);
}

static void decals(builder *b, int kind, float x0, float top, float side)
{
    int i;
    side += 0.01f;                                   /* just proud of the panel */
    switch (kind) {
    case DECAL_STRIPES:
        quad2(b, x0 + 0.1f, 1.45f, 4.85f, 1.62f, side, 0xFFFFFFu);
        quad2(b, x0 + 0.1f, 1.78f, 4.85f, 1.95f, side, 0xFFFFFFu);
        break;
    case DECAL_FLAMES:                               /* tongues licking back from the front */
        for (i = 0; i < 5; i++) {
            float y = 1.05f + i * 0.42f, len = 1.4f + 0.5f * ((i * 3) % 4);
            const float outer[3][2] = { { 4.85f, y - 0.2f }, { 4.85f, y + 0.2f }, { 4.85f - len, y + 0.05f } };
            const float inner[3][2] = { { 4.85f, y - 0.1f }, { 4.85f, y + 0.1f }, { 4.85f - len * 0.6f, y + 0.03f } };
            decal(b, outer, 3, side, 0xFF6A1Eu);
            decal(b, inner, 3, side + 0.005f, 0xFFD23Fu);
        }
        break;
    case DECAL_LIGHTNING: {
        const float a[4][2] = { { x0 + 0.3f, 2.9f }, { 2.9f, 2.25f }, { 2.7f, 2.05f }, { x0 + 0.2f, 2.65f } };
        const float c[4][2] = { { 2.5f, 2.3f }, { 3.0f, 2.15f }, { 4.7f, 1.05f }, { 2.4f, 2.0f } };
        decal(b, a, 4, side, 0xFFE23Au);
        decal(b, c, 4, side, 0xFFE23Au);
        break;
    }
    case DECAL_STARS:                                /* diamonds, big and small */
        for (i = 0; i < 6; i++) {
            float cx = x0 + 0.5f + i * ((4.6f - x0) / 6), cy = 1.3f + ((i * 5) % 3) * 0.6f, r = i & 1 ? 0.18f : 0.3f;
            const float d[4][2] = { { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy } };
            decal(b, d, 4, side, i & 1 ? 0xFFD23Fu : 0xFFFFFFu);
        }
        break;
    default:
        break;
    }
    DGK_UNUSED(top);
}

void lorry_style(lorry_meshes *m, uint32_t paint, int decal_kind, int cab)
{
    static const uint32_t rainbow[6] = { 0xE8402Au, 0xFF9A1Eu, 0xFFD23Fu, 0x5FD05Au, 0x3A9AE8u, 0x9A5AE8u };
    const float x0 = cab == CAB_BIG ? 1.4f : 1.6f, top = cab == CAB_BIG ? 3.9f : 3.4f;
    builder b;
    int i;
    mb_init(&b, cp, cc, ci, TV, TT);
    if (paint)
        mb_box(&b, x0, 0.8f, -1.2f, 4.9f, top, 1.2f, paint);
    else
        for (i = 0; i < 6; i++)                      /* the rainbow: front to back in slices */
            mb_box(&b, x0 + (4.9f - x0) * i / 6, 0.8f, -1.2f, x0 + (4.9f - x0) * (i + 1) / 6, top, 1.2f, rainbow[i]);
    mb_box(&b, 4.6f, 1.8f, -1.0f, 5.0f, 2.9f, 1.0f, 0x9FD8FFu);     /* windscreen */
    for (i = -1; i <= 1; i++)                        /* roof marker lights */
        mb_box(&b, 4.6f, top, i * 0.6f - 0.12f, 4.8f, top + 0.12f, i * 0.6f + 0.12f, 0xFFB030u);
    if (cab == CAB_BIG) {                            /* a sleeper's roof and twin chrome stacks */
        mb_box(&b, x0, top, -1.1f, 3.6f, top + 0.55f, 1.1f, paint ? mb_shade(paint, 0.85f) : 0xFFFFFFu);
        mb_box(&b, x0 + 0.05f, top - 1.0f, 0.95f, x0 + 0.35f, top + 1.0f, 1.25f, 0xE8ECF4u);
        mb_box(&b, x0 + 0.05f, top - 1.0f, -1.25f, x0 + 0.35f, top + 1.0f, -0.95f, 0xE8ECF4u);
    } else
        mb_box(&b, x0 + 0.1f, top, 0.6f, x0 + 0.4f, top + 0.9f, 0.9f, 0xC8C8D0u);   /* exhaust stack */
    decals(&b, decal_kind, x0, top, 1.2f);
    mb_mesh(&m->cab, &b);
}

void lorry_build(lorry_meshes *m)
{
    builder b;
    int s, t;
    /* The tractor's chassis: rear axle at x = 0, front axle at RIG_WHEELBASE. */
    mb_init(&b, tp, tc, ti, TV, TT);
    mb_box(&b, -1.1f, 0.7f, -1.0f, 4.9f, 1.1f, 1.0f, 0x3A3A44u);    /* chassis */
    mb_box(&b, -0.5f, 1.1f, -0.8f, 0.9f, 1.25f, 0.8f, 0x26262Cu);   /* fifth wheel */
    mb_box(&b, 4.9f, 0.55f, -1.25f, 5.15f, 1.05f, 1.25f, 0xC8C8D0u); /* bumper */
    mb_box(&b, 2.2f, 0.6f, -1.3f, 3.2f, 1.2f, -1.0f, 0xC8C8D0u);    /* fuel tanks */
    mb_box(&b, 2.2f, 0.6f, 1.0f, 3.2f, 1.2f, 1.3f, 0xC8C8D0u);
    for (s = -1; s <= 1; s += 2) {
        wheel(&b, RIG_WHEELBASE, (float)s);
        wheel(&b, 0.0f, (float)s);
    }
    mb_mesh(&m->chassis, &b);
    lorry_style(m, 0xE8402Au, DECAL_NONE, CAB_STANDARD);
    /* Trailers: kingpin at x = 0, axles about RIG_TRAILER_LEN behind. */
    for (t = 0; t < TRAILER_TYPES; t++) {
        mb_init(&b, rp[t], rc[t], ri[t], TV, TT);
        if (t == TRAILER_BOX) {
            mb_box(&b, -12.6f, 1.2f, -1.28f, 1.0f, 4.2f, 1.28f, 0xFFD23Fu);    /* box body */
            mb_box(&b, -12.5f, 4.2f, -1.2f, 0.9f, 4.35f, 1.2f, 0xFFFFFFu);     /* roof trim */
        } else if (t == TRAILER_FLATBED) {
            mb_box(&b, -12.6f, 1.2f, -1.28f, 1.0f, 1.55f, 1.28f, 0x3A6FD8u);   /* deck */
            mb_box(&b, 0.6f, 1.55f, -1.2f, 1.0f, 3.2f, 1.2f, 0xC8C8D0u);       /* headboard */
            mb_box(&b, -12.0f, 1.55f, -1.1f, -7.2f, 3.3f, 1.1f, 0xE0913Cu);    /* crates */
            mb_box(&b, -6.4f, 1.55f, -1.1f, -1.6f, 2.8f, 1.1f, 0xF2B45Au);
            mb_box(&b, -9.8f, 3.3f, -1.15f, -9.4f, 3.36f, 1.15f, 0x26262Cu);   /* straps */
            mb_box(&b, -4.2f, 2.8f, -1.15f, -3.8f, 2.86f, 1.15f, 0x26262Cu);
        } else {
            mb_box(&b, -12.6f, 1.1f, -0.9f, 1.0f, 1.4f, 0.9f, 0x3A3A44u);      /* chassis */
            tank(&b, -12.4f, 0.6f, 2.65f, 1.3f, 0xDCE3EAu, 0xE8402Au);
            mb_box(&b, -10.0f, 3.9f, -0.35f, -2.0f, 4.05f, 0.35f, 0x8A8F99u);  /* walkway */
        }
        mb_box(&b, -11.0f, 0.9f, -1.0f, -7.0f, 1.2f, 1.0f, 0x3A3A44u);         /* running gear */
        mb_box(&b, -2.4f, 0.0f, -1.0f, -2.1f, 1.2f, -0.7f, 0x55555Fu);         /* landing legs */
        mb_box(&b, -2.4f, 0.0f, 0.7f, -2.1f, 1.2f, 1.0f, 0x55555Fu);
        for (s = -1; s <= 1; s += 2) {
            wheel(&b, -RIG_TRAILER_LEN + 1.4f, (float)s);
            wheel(&b, -RIG_TRAILER_LEN, (float)s);
            wheel(&b, -RIG_TRAILER_LEN - 1.4f, (float)s);
        }
        mb_mesh(&m->trailer[t], &b);
    }
}

static float ground(const world *w, float x, float y)
{
    return w ? world_height(w, x, y) : 0.0f;          /* no world: flat (the garage's turntable) */
}

/* Posed on the ground: the tractor pitched between its axles and rolled
 * across its wheels, the trailer pitched from the fifth wheel to its axles
 * (the physics stays flat: this is only how it looks). */
void lorry_draw(const lorry_meshes *m, const rig *r, const world *w, const cab_sway *sway)
{
    float c = cosf(r->heading), s = sinf(r->heading), hx, hy, ax, ay;
    float h_rear = ground(w, r->x, r->y);
    float h_front = ground(w, r->x + RIG_WHEELBASE * c, r->y + RIG_WHEELBASE * s);
    float h_left = ground(w, r->x + 1.9f * c - 1.2f * s, r->y + 1.9f * s + 1.2f * c);
    float h_right = ground(w, r->x + 1.9f * c + 1.2f * s, r->y + 1.9f * s - 1.2f * c);
    float pitch = atan2f(h_front - h_rear, RIG_WHEELBASE) * 57.29578f;
    float roll = atan2f(h_left - h_right, 2.4f) * 57.29578f;
    float h_hitch;
    glPushMatrix();
    glTranslatef(r->x, h_rear, -r->y);
    glRotatef(r->heading * 57.29578f, 0, 1, 0);
    glRotatef(pitch, 0, 0, 1);
    glRotatef(-roll, 1, 0, 0);
    dgk_gfx_draw_mesh(&m->chassis);
    if (sway) {                                      /* the cab rocks about its base, on the chassis */
        glTranslatef(3.2f, 0.8f + sway->lift, 0);
        glRotatef(sway->pitch, 0, 0, 1);
        glRotatef(-sway->roll, 1, 0, 0);
        glTranslatef(-3.2f, -0.8f, 0);
    }
    dgk_gfx_draw_mesh(&m->cab);
    glPopMatrix();
    if (!r->has_trailer)
        return;
    rig_hitch(r, &hx, &hy);
    rig_trailer_axle(r, &ax, &ay);
    h_hitch = h_rear + (h_front - h_rear) * RIG_HITCH / RIG_WHEELBASE;
    glPushMatrix();
    glTranslatef(hx, h_hitch, -hy);
    glRotatef(r->trailer_heading * 57.29578f, 0, 1, 0);
    glRotatef(atan2f(h_hitch - ground(w, ax, ay), RIG_TRAILER_LEN) * 57.29578f, 0, 0, 1);
    dgk_gfx_draw_mesh(&m->trailer[r->trailer_type]);
    glPopMatrix();
}

void lorry_draw_trailer(const lorry_meshes *m, int type, float kx, float ky, float heading, const world *w)
{
    float h = ground(w, kx, ky);
    float ah = ground(w, kx - RIG_TRAILER_LEN * cosf(heading), ky - RIG_TRAILER_LEN * sinf(heading));
    glPushMatrix();
    glTranslatef(kx, h, -ky);
    glRotatef(heading * 57.29578f, 0, 1, 0);
    glRotatef(atan2f(h - ah, RIG_TRAILER_LEN) * 57.29578f, 0, 0, 1);
    dgk_gfx_draw_mesh(&m->trailer[type]);
    glPopMatrix();
}
