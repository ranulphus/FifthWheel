/* lorry.c - the toy lorry: a stubby cab in the player's paint (and decal,
 * and the big cab if bought) on a chassis with big wheels; a tall box
 * trailer, a flatbed with crates, a tanker. */
#include "lorry.h"
#include <GL/gl.h>
#include <math.h>

#define TV 2048
#define TT 2048
static float tp[TV * 3], cp[TV * 3], rp[TRAILER_TYPES][TV * 3];
static float tn[TV * 3], cn[TV * 3], rn[TRAILER_TYPES][TV * 3];
static uint32_t tb[TV], cb[TV], rb[TRAILER_TYPES][TV];
static uint8_t tc[TV * 4], cc[TV * 4], rc[TRAILER_TYPES][TV * 4];
static uint16_t ti[TT * 3], ci[TT * 3], ri[TRAILER_TYPES][TT * 3];

/* The models' roundness (detail.h's `models`): 0 boxes as they always were,
 * 1 rounded, 2 rounder. Big parts round their edges in q steps a quarter
 * turn, small ones in at most one; wheels and lamps have 8 or 12 sides. */
static int k_big(int q) { return q; }
static int k_small(int q) { return q > 1 ? 1 : q; }
static int sides(int q) { return q > 1 ? 12 : 8; }

/* A wheel: x along the lorry, side -1 left (-z) or +1 right; a dark tyre
 * of n sides (q > 0) with a bright hub on its outer face. */
static void wheel(builder *b, float x, float side, int q, int n)
{
    float z0 = side < 0 ? -1.55f : 1.05f, c[3], h[3];
    if (!q) {
        mb_box(b, x - 0.65f, 0.0f, z0, x + 0.65f, 1.3f, z0 + 0.5f, 0x26262Cu);
        mb_box(b, x - 0.25f, 0.45f, side < 0 ? z0 - 0.05f : z0 + 0.5f, x + 0.25f, 0.85f, side < 0 ? z0 : z0 + 0.55f,
               0xD0D4DCu);
        return;
    }
    c[0] = x; c[1] = 0.65f; c[2] = z0 + 0.25f;
    mb_cylinder(b, 2, c, 0.65f, 0.25f, n, 0x2A2A30u, 0x34343Cu, side < 0 ? 2 : 1);
    h[0] = x; h[1] = 0.65f; h[2] = side < 0 ? z0 - 0.02f : z0 + 0.52f;
    mb_cylinder(b, 2, h, 0.34f, 0.0f, 8, 0xE4E8F0u, 0xE4E8F0u, side < 0 ? 2 : 1);
}

/* A tank along x from x0 to x1, its middle cy up, radius r: octagonal with
 * flat ends (q 0), or round with its ends stepped in like a dome. */
static void tank(builder *b, float x0, float x1, float cy, float r, uint32_t rgb, uint32_t ends, int q)
{
    float ring[8][2], p[4][3], qq[4][3], c[3];
    int i, k;
    if (q) {
        c[0] = (x0 + x1) * 0.5f; c[1] = cy; c[2] = 0;
        mb_cylinder(b, 0, c, r, (x1 - x0) * 0.5f, sides(q) + 4, rgb, ends, 0);
        for (k = 0; k < 2; k++) {                    /* each end: a ring stepped in, then a smaller cap */
            c[0] = k ? x1 + 0.12f : x0 - 0.12f;
            mb_cylinder(b, 0, c, r * 0.88f, 0.12f, sides(q) + 4, ends, ends, k ? 1 : 2);
            c[0] = k ? x1 + 0.3f : x0 - 0.3f;
            mb_cylinder(b, 0, c, r * 0.6f, 0.06f, sides(q) + 4, ends, ends, k ? 1 : 2);
        }
        return;
    }
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
                qq[k][0] = x0; qq[k][1] = ring[bk][0]; qq[k][2] = ring[bk][1];    /* back, facing -x */
            }
            mb_quad(b, p[0], p[1], p[2], p[3], ends);
            mb_quad(b, qq[0], qq[1], qq[2], qq[3], ends);
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

/* The cab's decals, on the flat of its sides: from x0 back to x1 at the
 * front, y0 up to y1 (inside the rounded edges). */
static void decals(builder *b, int kind, float x0, float x1, float y0, float y1, float side)
{
    int i;
    float h = y1 - y0;
    side += 0.01f;                                   /* just proud of the panel */
    switch (kind) {
    case DECAL_STRIPES:
        quad2(b, x0 + 0.1f, y0 + h * 0.3f, x1, y0 + h * 0.38f, side, 0xFFFFFFu);
        quad2(b, x0 + 0.1f, y0 + h * 0.5f, x1, y0 + h * 0.58f, side, 0xFFFFFFu);
        break;
    case DECAL_FLAMES:                               /* tongues licking back from the front */
        for (i = 0; i < 4; i++) {
            float y = y0 + h * (0.16f + i * 0.24f), len = 1.4f + 0.5f * ((i * 3) % 4), t = h * 0.1f;
            const float outer[3][2] = { { x1, y - t }, { x1, y + t }, { x1 - len, y + t * 0.25f } };
            const float inner[3][2] = { { x1, y - t * 0.5f }, { x1, y + t * 0.5f }, { x1 - len * 0.6f, y + t * 0.15f } };
            decal(b, outer, 3, side, 0xFF6A1Eu);
            decal(b, inner, 3, side + 0.005f, 0xFFD23Fu);
        }
        break;
    case DECAL_LIGHTNING: {
        float m = (x0 + x1) * 0.5f;
        const float a[4][2] = { { x0 + 0.2f, y1 }, { m + 0.1f, y0 + h * 0.62f }, { m - 0.1f, y0 + h * 0.52f },
                                { x0 + 0.1f, y1 - h * 0.15f } };
        const float c[4][2] = { { m - 0.3f, y0 + h * 0.6f }, { m + 0.2f, y0 + h * 0.66f }, { x1, y0 },
                                { m - 0.4f, y0 + h * 0.5f } };
        decal(b, a, 4, side, 0xFFE23Au);
        decal(b, c, 4, side, 0xFFE23Au);
        break;
    }
    case DECAL_STARS:                                /* diamonds, big and small */
        for (i = 0; i < 6; i++) {
            float cx = x0 + 0.3f + i * ((x1 - x0 - 0.3f) / 6), cy = y0 + h * (0.25f + ((i * 5) % 3) * 0.25f);
            float r = i & 1 ? 0.18f : 0.3f;
            const float d[4][2] = { { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy } };
            decal(b, d, 4, side, i & 1 ? 0xFFD23Fu : 0xFFFFFFu);
        }
        break;
    default:
        break;
    }
}

/* A rainbow front to back over the vertices from `first` on, x0 to x1. */
static void rainbow_by_x(builder *b, int first, float x0, float x1)
{
    static const uint32_t bands[6] = { 0x9A5AE8u, 0x3A9AE8u, 0x5FD05Au, 0xFFD23Fu, 0xFF9A1Eu, 0xE8402Au };
    int i;
    for (i = first; i < b->nverts; i++) {
        float t = DGK_CLAMP((b->pos[i * 3] - x0) / (x1 - x0), 0.0f, 1.0f) * 5.0f;
        int k = (int)t < 5 ? (int)t : 4;
        float f = t - k;
        uint32_t a = bands[k], c = bands[k + 1], rgb = 0;
        int sh;
        for (sh = 0; sh <= 16; sh += 8) {
            float ca = (float)((a >> sh) & 255), cc2 = (float)((c >> sh) & 255);
            rgb |= (uint32_t)(ca + (cc2 - ca) * f) << sh;
        }
        b->base[i] = rgb;
    }
}

void lorry_style(lorry_meshes *m, uint32_t paint, int decal_kind, int cab)
{
    static const uint32_t rainbow[6] = { 0xE8402Au, 0xFF9A1Eu, 0xFFD23Fu, 0x5FD05Au, 0x3A9AE8u, 0x9A5AE8u };
    const float x0 = cab == CAB_BIG ? 1.4f : 1.6f, top = cab == CAB_BIG ? 3.9f : 3.4f;
    const int q = m->quality;
    builder *b = &m->cab_b;
    int i, s;
    mb_init(b, cp, cc, ci, TV, TT);
    mb_normals(b, cn, cb);
    if (!q) {                                        /* the boxes, as they always were */
        if (paint)
            mb_box(b, x0, 0.8f, -1.2f, 4.9f, top, 1.2f, paint);
        else
            for (i = 0; i < 6; i++)                  /* the rainbow: front to back in slices */
                mb_box(b, x0 + (4.9f - x0) * i / 6, 0.8f, -1.2f, x0 + (4.9f - x0) * (i + 1) / 6, top, 1.2f, rainbow[i]);
        mb_box(b, 4.6f, 1.8f, -1.0f, 5.0f, 2.9f, 1.0f, 0x9FD8FFu);     /* windscreen */
        for (i = -1; i <= 1; i++)                    /* roof marker lights */
            mb_box(b, 4.6f, top, i * 0.6f - 0.12f, 4.8f, top + 0.12f, i * 0.6f + 0.12f, 0xFFB030u);
        if (cab == CAB_BIG) {                        /* a sleeper's roof and twin chrome stacks */
            mb_box(b, x0, top, -1.1f, 3.6f, top + 0.55f, 1.1f, paint ? mb_shade(paint, 0.85f) : 0xFFFFFFu);
            mb_box(b, x0 + 0.05f, top - 1.0f, 0.95f, x0 + 0.35f, top + 1.0f, 1.25f, 0xE8ECF4u);
            mb_box(b, x0 + 0.05f, top - 1.0f, -1.25f, x0 + 0.35f, top + 1.0f, -0.95f, 0xE8ECF4u);
        } else
            mb_box(b, x0 + 0.1f, top, 0.6f, x0 + 0.4f, top + 0.9f, 0.9f, 0xC8C8D0u);   /* exhaust stack */
        decals(b, decal_kind, x0 + 0.1f, 4.85f, 1.05f, top - 0.25f, 1.2f);
        mb_mesh(&m->cab, b);
        return;
    }
    {
        /* The cab: a rounded body; the windscreen wraps its front, side
         * windows behind it, a grille and round lamps below. */
        const float r = 0.42f;
        int first = b->nverts;
        float c[3];
        mb_rbox(b, x0, 0.8f, -1.2f, 4.9f, top, 1.2f, r, k_big(q), paint ? paint : 0xFFFFFFu);
        if (!paint)
            rainbow_by_x(b, first, x0, 4.9f);
        mb_rbox(b, 4.78f, top - 1.45f, -1.02f, 4.98f, top - 0.32f, 1.02f, 0.1f, k_small(q), 0x7EC8F2u);
        mb_rbox(b, 4.96f, top - 0.95f, 0.15f, 5.0f, top - 0.45f, 0.45f, 0.02f, 0, 0xE8F6FFu);   /* the glint */
        for (s = -1; s <= 1; s += 2) {
            mb_rbox(b, 3.9f, top - 1.4f, s < 0 ? -1.24f : 1.12f, 4.55f, top - 0.38f, s < 0 ? -1.12f : 1.24f, 0.08f,
                    0, 0x7EC8F2u);
            /* A mirror on an arm. */
            mb_rbox(b, 4.62f, top - 1.0f, s < 0 ? -1.5f : 1.18f, 4.7f, top - 0.92f, s < 0 ? -1.18f : 1.5f, 0.03f, 0,
                    0x26262Cu);
            mb_rbox(b, 4.5f, top - 1.55f, s < 0 ? -1.62f : 1.44f, 4.72f, top - 0.8f, s < 0 ? -1.44f : 1.62f, 0.07f,
                    0, 0x26262Cu);
            /* A round headlamp, a small amber one beside it. */
            c[0] = 4.93f; c[1] = 1.22f; c[2] = s * 0.86f;
            mb_cylinder(b, 0, c, 0.2f, 0.06f, sides(q), 0xD8DCE4u, 0xFFF6C8u, 1);
            mb_rbox(b, 4.9f, 1.12f, s < 0 ? -1.18f : 1.04f, 4.99f, 1.32f, s < 0 ? -1.04f : 1.18f, 0.04f, 0, 0xFFA020u);
        }
        mb_rbox(b, 4.86f, 1.05f, -0.58f, 4.97f, 1.75f, 0.58f, 0.06f, 0, 0x45454Fu);            /* grille */
        for (i = 0; i < 3; i++)
            mb_rbox(b, 4.95f, 1.16f + i * 0.2f, -0.52f, 5.0f, 1.24f + i * 0.2f, 0.52f, 0.0f, 0, 0xD8DCE4u);
        for (i = -1; i <= 1; i++)                    /* roof marker lights */
            mb_rbox(b, 4.45f, top - 0.04f, i * 0.55f - 0.13f, 4.7f, top + 0.12f, i * 0.55f + 0.13f, 0.05f, 0,
                    0xFFB030u);
        if (cab == CAB_BIG) {                        /* a sleeper's pod and twin chrome stacks */
            mb_rbox(b, x0 + 0.1f, top - 0.2f, -1.1f, 3.7f, top + 0.6f, 1.1f, 0.35f, k_big(q),
                    paint ? mb_shade(paint, 0.88f) : 0xFFFFFFu);
            for (s = -1; s <= 1; s += 2) {
                c[0] = x0 + 0.22f; c[1] = top + 0.2f; c[2] = s * 1.12f;
                mb_cylinder(b, 1, c, 0.13f, 1.2f, sides(q), 0xE8ECF4u, 0x45454Fu, 1);
            }
        } else {
            c[0] = x0 + 0.25f; c[1] = top + 0.25f; c[2] = 0.78f;                   /* exhaust stack */
            mb_cylinder(b, 1, c, 0.13f, 0.75f, sides(q), 0xD8DCE4u, 0x45454Fu, 1);
        }
        decals(b, decal_kind, x0 + r, 4.9f - r - 0.15f, 0.8f + r, top - 1.5f, 1.2f);
        mb_relight(b, 0);
        mb_mesh(&m->cab, b);
    }
}

void lorry_build(lorry_meshes *m, int quality)
{
    builder *b = &m->chassis_b;
    int s, t, q = m->quality = DGK_CLAMP(quality, 0, 2);
    float c[3];
    /* The tractor's chassis: rear axle at x = 0, front axle at RIG_WHEELBASE. */
    mb_init(b, tp, tc, ti, TV, TT);
    mb_normals(b, tn, tb);
    mb_rbox(b, -1.1f, 0.7f, -1.0f, 4.9f, 1.1f, 1.0f, 0.12f, k_small(q), 0x3A3A44u);   /* chassis */
    if (q) {
        c[0] = 0.2f; c[1] = 1.15f; c[2] = 0;                                       /* fifth wheel */
        mb_cylinder(b, 1, c, 0.75f, 0.08f, sides(q), 0x26262Cu, 0x34343Cu, 1);
        mb_rbox(b, 4.85f, 0.5f, -1.27f, 5.25f, 1.05f, 1.27f, 0.2f, k_big(q), 0xD8DCE4u);   /* bumper */
        for (s = -1; s <= 1; s += 2) {
            c[0] = 2.7f; c[1] = 0.95f; c[2] = s * 1.12f;                           /* fuel tanks */
            mb_cylinder(b, 0, c, 0.34f, 0.55f, sides(q), 0xD0D4DCu, 0xB8BCC6u, 3);
            mb_rbox(b, -0.75f, 1.32f, s < 0 ? -1.62f : 0.98f, 0.75f, 1.5f, s < 0 ? -0.98f : 1.62f, 0.08f, 0,
                    0x2A2A30u);                                                     /* a mudguard */
        }
    } else {
        mb_box(b, -0.5f, 1.1f, -0.8f, 0.9f, 1.25f, 0.8f, 0x26262Cu);   /* fifth wheel */
        mb_box(b, 4.9f, 0.55f, -1.25f, 5.15f, 1.05f, 1.25f, 0xC8C8D0u); /* bumper */
        mb_box(b, 2.2f, 0.6f, -1.3f, 3.2f, 1.2f, -1.0f, 0xC8C8D0u);    /* fuel tanks */
        mb_box(b, 2.2f, 0.6f, 1.0f, 3.2f, 1.2f, 1.3f, 0xC8C8D0u);
    }
    for (s = -1; s <= 1; s += 2) {
        wheel(b, RIG_WHEELBASE, (float)s, q, sides(q));
        wheel(b, 0.0f, (float)s, q, sides(q));
    }
    mb_mesh(&m->chassis, b);
    lorry_style(m, 0xE8402Au, DECAL_NONE, CAB_STANDARD);
    /* Trailers: kingpin at x = 0, axles about RIG_TRAILER_LEN behind. */
    for (t = 0; t < TRAILER_TYPES; t++) {
        b = &m->trailer_b[t];
        mb_init(b, rp[t], rc[t], ri[t], TV, TT);
        mb_normals(b, rn[t], rb[t]);
        if (t == TRAILER_BOX) {
            mb_rbox(b, -12.6f, 1.2f, -1.28f, 1.0f, 4.2f, 1.28f, 0.3f, k_big(q), 0xFFD23Fu);   /* box body */
            if (q) {
                mb_rbox(b, -12.3f, 4.12f, -1.0f, 0.7f, 4.26f, 1.0f, 0.0f, 0, 0xFFFFFFu);         /* the roof */
                quad2(b, -11.9f, 2.55f, 0.3f, 2.85f, 1.29f, 0xFFFFFFu);                    /* a stripe each side */
                for (s = -1; s <= 1; s += 2)                                             /* the back's doors */
                    mb_rbox(b, -12.66f, 1.5f, s < 0 ? -1.0f : 0.04f, -12.55f, 3.9f, s < 0 ? -0.04f : 1.0f, 0.05f,
                            0, 0xF0C030u);
                mb_rbox(b, -12.75f, 0.85f, -1.25f, -12.35f, 1.2f, 1.25f, 0.08f, 0, 0x3A3A44u);           /* bar */
                for (s = -1; s <= 1; s += 2)                                             /* tail lamps */
                    mb_rbox(b, -12.8f, 0.92f, s < 0 ? -1.15f : 0.75f, -12.7f, 1.12f, s < 0 ? -0.75f : 1.15f, 0.03f,
                            0, 0xE8402Au);
            } else
                mb_box(b, -12.5f, 4.2f, -1.2f, 0.9f, 4.35f, 1.2f, 0xFFFFFFu);     /* roof trim */
        } else if (t == TRAILER_FLATBED) {
            mb_rbox(b, -12.6f, 1.2f, -1.28f, 1.0f, 1.55f, 1.28f, 0.1f, k_small(q), 0x3A6FD8u);  /* deck */
            mb_rbox(b, 0.6f, 1.55f, -1.2f, 1.0f, 3.2f, 1.2f, 0.12f, k_small(q), 0xC8C8D0u);    /* headboard */
            mb_rbox(b, -12.0f, 1.55f, -1.1f, -7.2f, 3.3f, 1.1f, 0.18f, k_big(q), 0xE0913Cu);   /* crates */
            mb_rbox(b, -6.4f, 1.55f, -1.1f, -1.6f, 2.8f, 1.1f, 0.18f, k_big(q), 0xF2B45Au);
            mb_box(b, -9.8f, 3.3f, -1.15f, -9.4f, 3.36f, 1.15f, 0x26262Cu);   /* straps */
            mb_box(b, -4.2f, 2.8f, -1.15f, -3.8f, 2.86f, 1.15f, 0x26262Cu);
        } else {
            mb_rbox(b, -12.6f, 1.1f, -0.9f, 1.0f, 1.4f, 0.9f, 0.08f, k_small(q), 0x3A3A44u);   /* chassis */
            tank(b, -12.4f, 0.6f, 2.65f, 1.3f, 0xDCE3EAu, 0xE8402Au, q);
            mb_rbox(b, -10.0f, 3.9f, -0.35f, -2.0f, 4.05f, 0.35f, 0.04f, k_small(q), 0x8A8F99u);  /* walkway */
        }
        mb_box(b, -11.0f, 0.9f, -1.0f, -7.0f, 1.2f, 1.0f, 0x3A3A44u);         /* running gear */
        mb_box(b, -2.4f, 0.0f, -1.0f, -2.1f, 1.2f, -0.7f, 0x55555Fu);         /* landing legs */
        mb_box(b, -2.4f, 0.0f, 0.7f, -2.1f, 1.2f, 1.0f, 0x55555Fu);
        for (s = -1; s <= 1; s += 2) {                /* under the body: 8 sides do */
            wheel(b, -RIG_TRAILER_LEN + 1.4f, (float)s, q, 8);
            wheel(b, -RIG_TRAILER_LEN, (float)s, q, 8);
            wheel(b, -RIG_TRAILER_LEN - 1.4f, (float)s, q, 8);
        }
        mb_mesh(&m->trailer[t], b);
    }
}

static float ground(const world *w, float x, float y)
{
    return w ? world_height(w, x, y) : 0.0f;          /* no world: flat (the garage's turntable) */
}

/* Posed on the ground: the tractor pitched between its axles and rolled
 * across its wheels, the trailer pitched from the fifth wheel to its axles
 * (the physics stays flat: this is only how it looks). */
void lorry_draw(lorry_meshes *m, const rig *r, const world *w, const cab_sway *sway)
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
    mb_draw(&m->chassis, &m->chassis_b, r->heading);
    if (sway) {                                      /* the cab rocks about its base, on the chassis */
        glTranslatef(3.2f, 0.8f + sway->lift, 0);
        glRotatef(sway->pitch, 0, 0, 1);
        glRotatef(-sway->roll, 1, 0, 0);
        glTranslatef(-3.2f, -0.8f, 0);
    }
    mb_draw(&m->cab, &m->cab_b, r->heading);
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
    mb_draw(&m->trailer[r->trailer_type], &m->trailer_b[r->trailer_type], r->trailer_heading);
    glPopMatrix();
}

void lorry_draw_trailer(lorry_meshes *m, int type, float kx, float ky, float heading, const world *w)
{
    float h = ground(w, kx, ky);
    float ah = ground(w, kx - RIG_TRAILER_LEN * cosf(heading), ky - RIG_TRAILER_LEN * sinf(heading));
    glPushMatrix();
    glTranslatef(kx, h, -ky);
    glRotatef(heading * 57.29578f, 0, 1, 0);
    glRotatef(atan2f(h - ah, RIG_TRAILER_LEN) * 57.29578f, 0, 0, 1);
    mb_draw(&m->trailer[type], &m->trailer_b[type], heading);
    glPopMatrix();
}

#define SHADOW_LIFT 0.3f        /* above the terrain: about where a road's surface is */
#define SHADOW_SOFT 0.8f        /* metres over which the edge fades */
#define SHADOW_ALPHA 120        /* of 255, under the middle */
#define SHADOW_EAST 0.55f       /* the offset to the south-east */

static float shadow_height(const world *w, float x, float y)
{
    return w ? world_height(w, x, y) + SHADOW_LIFT : 0.03f;
}

/* A footprint from `a0` to `a1` metres along `heading` from (x, y), `half`
 * wide either side: dark in the middle, fading to nothing across its edge
 * (four trapezoids round an inner quad), on the ground south-east of it. */
static void shadow(const world *w, float x, float y, float heading, float a0, float a1, float half)
{
    static const float sa[4] = { 0, 1, 1, 0 }, sc[4] = { -1, -1, 1, 1 };
    float c = cosf(heading), s = sinf(heading), in[4][3], out[4][3], f = SHADOW_SOFT * 0.5f;
    int k;
    x += SHADOW_EAST;
    y -= SHADOW_EAST;
    for (k = 0; k < 4; k++) {
        float ai = sa[k] ? a1 - f : a0 + f, ao = sa[k] ? a1 + f : a0 - f;
        float ci = sc[k] * (half - f), co = sc[k] * (half + f);
        in[k][0] = x + c * ai - s * ci;
        in[k][1] = y + s * ai + c * ci;
        out[k][0] = x + c * ao - s * co;
        out[k][1] = y + s * ao + c * co;
        in[k][2] = shadow_height(w, in[k][0], in[k][1]);
        out[k][2] = shadow_height(w, out[k][0], out[k][1]);
    }
    glColor4ub(0, 0, 0, SHADOW_ALPHA);
    for (k = 0; k < 4; k++)
        glVertex3f(in[k][0], in[k][2], -in[k][1]);
    for (k = 0; k < 4; k++) {
        int j = (k + 1) & 3;
        glColor4ub(0, 0, 0, 0);
        glVertex3f(out[k][0], out[k][2], -out[k][1]);
        glVertex3f(out[j][0], out[j][2], -out[j][1]);
        glColor4ub(0, 0, 0, SHADOW_ALPHA);
        glVertex3f(in[j][0], in[j][2], -in[j][1]);
        glVertex3f(in[k][0], in[k][2], -in[k][1]);
    }
    dgk_gfx_tris += 10;
}

void lorry_shadows_begin(void)
{
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
}

/* The tractor from its back (1.1 m behind the rear axle) to its bumper; with
 * a trailer, only ahead of the trailer's nose (the trailer's shadow covers
 * the rest, and two would darken where they overlap). */
void lorry_shadow_rig(const rig *r, const world *w)
{
    float hx, hy;
    shadow(w, r->x, r->y, r->heading, r->has_trailer ? RIG_HITCH + 1.0f : -1.1f, 4.9f, 1.25f);
    if (!r->has_trailer)
        return;
    rig_hitch(r, &hx, &hy);
    lorry_shadow_trailer(hx, hy, r->trailer_heading, w);
}

/* A trailer from its back to its nose, 1 m ahead of the kingpin. */
void lorry_shadow_trailer(float kx, float ky, float heading, const world *w)
{
    shadow(w, kx, ky, heading, -RIG_TRAILER_REAR, 1.0f, 1.28f);
}

void lorry_shadow_footprint(const world *w, float x, float y, float heading, float a0, float a1, float half)
{
    shadow(w, x, y, heading, a0, a1, half);
}

void lorry_shadows_end(void)
{
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
