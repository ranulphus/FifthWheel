/* fwgen - generate Fifth Wheel's world (plan milestone F3) from a seed:
 * gentle hills, towns with street grids, depots with loading bays, a road
 * network levelled into the hills, patchwork fields and trees, all in the
 * toy style (flat colours, baked warm light), written as a pack of 128 m
 * chunks (docs/formats.md, "The world").
 *
 *   fwgen SEED OUT.PAK        (prints the pack's hash: golden/world.sha)
 *
 * Deterministic: integer hashing, no libm trigonometry in decisions
 * (sinf/cosf only place geometry), -ffp-contract=off. */
#include "mesh.h"
#include "pakw.h"
#include "../game/src/wgen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- noise and the heightfield --------------------------------------- */

static uint32_t seed_g;

static uint32_t hash2(int x, int y, uint32_t salt)
{
    uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^ (seed_g + salt) * 0xCB1AB31Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

static float lattice(int x, int y, uint32_t salt)
{
    return (hash2(x, y, salt) & 0xFFFFFF) / 8388607.5f - 1.0f;           /* -1..1 */
}

static float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static float value_noise(float x, float y, uint32_t salt)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = smooth(x - ix), fy = smooth(y - iy);
    float a = lattice(ix, iy, salt), b = lattice(ix + 1, iy, salt);
    float c = lattice(ix, iy + 1, salt), d = lattice(ix + 1, iy + 1, salt);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static float fbm(float x, float y)
{
    float sum = 0, amp = 1, f = 1.0f / 900.0f;
    int o;
    for (o = 0; o < 4; o++) {
        sum += amp * value_noise(x * f, y * f, (uint32_t)o * 101u);
        amp *= 0.45f;
        f *= 2.1f;
    }
    return sum;
}

static float height[WG_HN * WG_HN];                /* metres, WG_CELL apart */
static float road_weight[WG_HN * WG_HN];           /* 0..1: levelled for a road */
static float road_height[WG_HN * WG_HN];

static float h_at(int i, int j)
{
    i = i < 0 ? 0 : i >= WG_HN ? WG_HN - 1 : i;
    j = j < 0 ? 0 : j >= WG_HN ? WG_HN - 1 : j;
    return height[j * WG_HN + i];
}

/* Height anywhere, bilinear between the grid's samples. */
static float ground(float x, float y)
{
    float gx = x / WG_CELL, gy = y / WG_CELL;
    int i = (int)floorf(gx), j = (int)floorf(gy);
    float fx = gx - i, fy = gy - j;
    float a = h_at(i, j), b = h_at(i + 1, j), c = h_at(i, j + 1), d = h_at(i + 1, j + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static void make_hills(void)
{
    int i, j;
    for (j = 0; j < WG_HN; j++)
        for (i = 0; i < WG_HN; i++)
            height[j * WG_HN + i] = 18.0f * fbm(i * WG_CELL, j * WG_CELL) + 6.0f;
}

/* ---- places -------------------------------------------------------------- */

typedef struct place {
    float x, y, r, heading;
    int kind;                                     /* WG_TOWN, WG_DEPOT */
} place;

static place places[WG_MAX_PLACES];
static int nplaces, ntowns, ndepots;
static dgk_rng rng;

static float frand(void)
{
    return (dgk_rng_u32(&rng) >> 8) / 16777216.0f;
}

static int far_from_all(float x, float y, float d)
{
    int i;
    for (i = 0; i < nplaces; i++) {
        float dx = places[i].x - x, dy = places[i].y - y;
        if (dx * dx + dy * dy < d * d)
            return 0;
    }
    return 1;
}

static void place_kind(int kind, int count, float spacing, float margin, float r0, float r1)
{
    int n = 0, tries;
    for (tries = 0; tries < 20000 && n < count; tries++) {
        float x = margin + frand() * (WG_SIZE - 2 * margin), y = margin + frand() * (WG_SIZE - 2 * margin);
        if (!far_from_all(x, y, spacing))
            continue;
        places[nplaces].x = x;
        places[nplaces].y = y;
        places[nplaces].r = r0 + frand() * (r1 - r0);
        places[nplaces].heading = (float)(dgk_rng_below(&rng, 4)) * WG_PI / 2;   /* grids on the axes */
        places[nplaces].kind = kind;
        nplaces++;
        n++;
    }
}

/* ---- geometry per chunk --------------------------------------------------- */

typedef struct cmesh {
    float *pos;                                   /* GL coordinates, world */
    uint8_t *rgba;
    uint16_t *idx;
    int nverts, ntris, cap_v, cap_t;
} cmesh;

static cmesh chunks[WG_CHUNKS * WG_CHUNKS];
static long total_tris;

static cmesh *chunk_for(float x, float y)
{
    int cx = (int)(x / WG_CHUNK), cy = (int)(y / WG_CHUNK);
    cx = cx < 0 ? 0 : cx >= WG_CHUNKS ? WG_CHUNKS - 1 : cx;
    cy = cy < 0 ? 0 : cy >= WG_CHUNKS ? WG_CHUNKS - 1 : cy;
    return &chunks[cy * WG_CHUNKS + cx];
}

/* A quad in GL coordinates (x, height, -y), anticlockwise seen from its
 * front; into the chunk holding its centre. light < 0: the sun's. */
static void quad(const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb, float light)
{
    float cx = (p0[0] + p1[0] + p2[0] + p3[0]) / 4, cy = -(p0[2] + p1[2] + p2[2] + p3[2]) / 4;
    cmesh *c = chunk_for(cx, cy);
    builder b;
    if (c->nverts + 4 > c->cap_v) {
        c->cap_v = c->cap_v ? c->cap_v * 2 : 1024;
        c->pos = (float *)realloc(c->pos, (size_t)c->cap_v * 12);
        c->rgba = (uint8_t *)realloc(c->rgba, (size_t)c->cap_v * 4);
    }
    if (c->ntris + 2 > c->cap_t) {
        c->cap_t = c->cap_t ? c->cap_t * 2 : 1024;
        c->idx = (uint16_t *)realloc(c->idx, (size_t)c->cap_t * 6);
    }
    /* Borrow the mesh builder's lighting, then take its output as ours. */
    mb_init(&b, c->pos, c->rgba, c->idx, c->cap_v, c->cap_t);
    b.nverts = c->nverts;
    b.ntris = c->ntris;
    if (light < 0)
        mb_quad(&b, p0, p1, p2, p3, rgb);
    else
        mb_quad_lit(&b, p0, p1, p2, p3, rgb, light);
    c->nverts = b.nverts;
    c->ntris = b.ntris;
    total_tris += 2;
}

/* A flat quad lying on the ground (plus lift), corners in ground coordinates. */
static void ground_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float lift,
                        uint32_t rgb, float light)
{
    float p[4][3] = { { x0, ground(x0, y0) + lift, -y0 }, { x1, ground(x1, y1) + lift, -y1 },
                      { x2, ground(x2, y2) + lift, -y2 }, { x3, ground(x3, y3) + lift, -y3 } };
    quad(p[0], p[1], p[2], p[3], rgb, light);
}

/* ---- collision boxes -------------------------------------------------------- */

typedef struct box2 {
    float cx, cy, hl, hw, angle;
} box2;
static box2 *boxes;
static int nboxes, cap_boxes;

static void add_box(float cx, float cy, float hl, float hw, float angle)
{
    if (nboxes == cap_boxes) {
        cap_boxes = cap_boxes ? cap_boxes * 2 : 1024;
        boxes = (box2 *)realloc(boxes, sizeof *boxes * (size_t)cap_boxes);
    }
    boxes[nboxes].cx = cx; boxes[nboxes].cy = cy; boxes[nboxes].hl = hl; boxes[nboxes].hw = hw;
    boxes[nboxes].angle = angle;
    nboxes++;
}

/* A block standing on the ground: centre, half length along `angle`, half
 * width, height; walls in `rgb`, the top in `top`; a contact shadow to the
 * south-east (the sun is north-west), and a collision box. */
static void block(float cx, float cy, float hl, float hw, float angle, float h, uint32_t rgb, uint32_t top,
                  int shadow, int collide)
{
    float c = cosf(angle), s = sinf(angle), k[4][2], base, v[8][3];
    int i;
    k[0][0] = cx - c * hl + s * hw; k[0][1] = cy - s * hl - c * hw;
    k[1][0] = cx + c * hl + s * hw; k[1][1] = cy + s * hl - c * hw;
    k[2][0] = cx + c * hl - s * hw; k[2][1] = cy + s * hl + c * hw;
    k[3][0] = cx - c * hl - s * hw; k[3][1] = cy - s * hl + c * hw;
    base = ground(cx, cy);
    for (i = 0; i < 4; i++) {                     /* sunk a little into slopes */
        float g = ground(k[i][0], k[i][1]);
        if (g < base)
            base = g;
    }
    if (shadow) {
        float off = h * 0.35f;
        uint32_t under = 0x3E6E2Eu;
        ground_quad(k[0][0] + off, k[0][1] - off, k[1][0] + off, k[1][1] - off, k[2][0] + off, k[2][1] - off,
                    k[3][0] + off, k[3][1] - off, 0.08f, under, 0.62f);
    }
    for (i = 0; i < 4; i++) {
        v[i][0] = v[i + 4][0] = k[i][0];
        v[i][2] = v[i + 4][2] = -k[i][1];
        v[i][1] = base - 0.5f;
        v[i + 4][1] = base + h;
    }
    quad(v[4], v[5], v[6], v[7], top, -1);
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        quad(v[i], v[j], v[j + 4], v[i + 4], rgb, -1);
    }
    if (collide)
        add_box(cx, cy, hl, hw, angle);
}

/* ---- roads ------------------------------------------------------------------------ */

#define MAX_EDGES 64
#define MAX_POINTS 20000
static wg_node nodes[WG_MAX_PLACES];
static wg_edge edges[MAX_EDGES];
static int nnodes, nedges;
static float pts[MAX_POINTS][3];                 /* x, y, road height */
static int npts;

static int connected(int a, int b)
{
    int i;
    for (i = 0; i < nedges; i++)
        if (((int)edges[i].a == a && (int)edges[i].b == b) || ((int)edges[i].a == b && (int)edges[i].b == a))
            return 1;
    return 0;
}

static float dist2(int a, int b)
{
    float dx = nodes[a].x - nodes[b].x, dy = nodes[a].y - nodes[b].y;
    return dx * dx + dy * dy;
}

/* A road from node a to b: a Catmull-Rom curve through jittered points,
 * sampled about every 8 m; its height profile the ground's, smoothed. */
static void add_road(int a, int b)
{
    float cp[7][2], len = sqrtf(dist2(a, b)), nx, ny, raw[4096];
    int n = 0, i, k, seg, first = npts, count;
    wg_edge *e = &edges[nedges];
    if (nedges == MAX_EDGES)
        return;
    nx = -(nodes[b].y - nodes[a].y) / len;
    ny = (nodes[b].x - nodes[a].x) / len;
    cp[0][0] = nodes[a].x; cp[0][1] = nodes[a].y;
    for (i = 1; i <= 4; i++) {
        float t = i / 5.0f, j = (frand() - 0.5f) * DGK_MIN(0.22f * len, 260.0f);
        cp[i][0] = DGK_CLAMP(nodes[a].x + (nodes[b].x - nodes[a].x) * t + nx * j, 60.0f, WG_SIZE - 60.0f);
        cp[i][1] = DGK_CLAMP(nodes[a].y + (nodes[b].y - nodes[a].y) * t + ny * j, 60.0f, WG_SIZE - 60.0f);
    }
    cp[5][0] = nodes[b].x; cp[5][1] = nodes[b].y;
    for (seg = 0; seg < 5; seg++) {
        const float *p0 = cp[seg > 0 ? seg - 1 : 0], *p1 = cp[seg], *p2 = cp[seg + 1], *p3 = cp[seg < 4 ? seg + 2 : 5];
        float sl = sqrtf((p2[0] - p1[0]) * (p2[0] - p1[0]) + (p2[1] - p1[1]) * (p2[1] - p1[1]));
        int steps = (int)(sl / 8.0f) + 1;
        for (k = 0; k < steps && npts < MAX_POINTS && n < 4096; k++) {
            float t = (float)k / steps, t2 = t * t, t3 = t2 * t;
            float x = 0.5f * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 +
                              (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3);
            float y = 0.5f * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 +
                              (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3);
            pts[npts][0] = x;
            pts[npts][1] = y;
            raw[n++] = ground(x, y);
            npts++;
        }
    }
    if (npts < MAX_POINTS && n < 4096) {
        pts[npts][0] = cp[5][0];
        pts[npts][1] = cp[5][1];
        raw[n++] = ground(cp[5][0], cp[5][1]);
        npts++;
    }
    count = npts - first;
    for (k = 0; k < 3; k++) {                    /* smooth the profile: gentle grades */
        float tmp[4096];
        for (i = 0; i < count; i++) {
            float sum = 0;
            int m, c = 0;
            for (m = -6; m <= 6; m++)
                if (i + m >= 0 && i + m < count) {
                    sum += raw[i + m];
                    c++;
                }
            tmp[i] = sum / c;
        }
        memcpy(raw, tmp, sizeof(float) * (size_t)count);
    }
    e->a = (uint32_t)a;
    e->b = (uint32_t)b;
    e->first_point = (uint32_t)first;
    e->npoints = (uint32_t)count;
    e->length = 0;
    for (i = 0; i < count; i++) {
        pts[first + i][2] = raw[i];
        if (i)
            e->length += sqrtf((pts[first + i][0] - pts[first + i - 1][0]) * (pts[first + i][0] - pts[first + i - 1][0]) +
                               (pts[first + i][1] - pts[first + i - 1][1]) * (pts[first + i][1] - pts[first + i - 1][1]));
    }
    nedges++;
}

/* Level the ground to a height along a segment: full weight within `flat`
 * metres, easing out by `ease`. */
static void level_segment(float x0, float y0, float h0, float x1, float y1, float h1, float flat, float ease)
{
    float r = flat + ease, dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy + 1e-6f;
    int i0 = (int)((DGK_MIN(x0, x1) - r) / WG_CELL), i1 = (int)((DGK_MAX(x0, x1) + r) / WG_CELL) + 1;
    int j0 = (int)((DGK_MIN(y0, y1) - r) / WG_CELL), j1 = (int)((DGK_MAX(y0, y1) + r) / WG_CELL) + 1;
    int i, j;
    for (j = DGK_MAX(j0, 0); j <= DGK_MIN(j1, WG_HN - 1); j++)
        for (i = DGK_MAX(i0, 0); i <= DGK_MIN(i1, WG_HN - 1); i++) {
            float px = i * WG_CELL, py = j * WG_CELL;
            float t = DGK_CLAMP(((px - x0) * dx + (py - y0) * dy) / l2, 0.0f, 1.0f);
            float qx = x0 + dx * t - px, qy = y0 + dy * t - py, d = sqrtf(qx * qx + qy * qy), w;
            if (d >= r)
                continue;
            w = d <= flat ? 1.0f : 1.0f - smooth((d - flat) / ease);
            if (w > road_weight[j * WG_HN + i]) {
                road_weight[j * WG_HN + i] = w;
                road_height[j * WG_HN + i] = h0 + (h1 - h0) * t;
            }
        }
}

static void apply_levelling(void)
{
    int i;
    for (i = 0; i < WG_HN * WG_HN; i++)
        height[i] += (road_height[i] - height[i]) * road_weight[i];
}

/* The road surface: a ribbon a little above the levelled ground, with bold
 * dashes down the middle. */
static void road_ribbon(const float (*p)[3], int n, float half, int dashes, float lift)
{
    int i;
    float along = 0;
    for (i = 0; i + 1 < n; i++) {
        float dx = p[i + 1][0] - p[i][0], dy = p[i + 1][1] - p[i][1], l = sqrtf(dx * dx + dy * dy);
        float nx0, ny0, nx1, ny1;
        if (l < 0.01f)
            continue;
        {
            /* The ribbon's width across the mean of neighbouring directions, so joints meet. */
            float ax = i > 0 ? p[i + 1][0] - p[i - 1][0] : dx, ay = i > 0 ? p[i + 1][1] - p[i - 1][1] : dy;
            float bx = i + 2 < n ? p[i + 2][0] - p[i][0] : dx, by = i + 2 < n ? p[i + 2][1] - p[i][1] : dy;
            float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
            nx0 = -ay / la * half; ny0 = ax / la * half;
            nx1 = -by / lb * half; ny1 = bx / lb * half;
        }
        {
            float q[4][3] = { { p[i][0] - nx0, p[i][2] + lift, -(p[i][1] - ny0) },
                              { p[i + 1][0] - nx1, p[i + 1][2] + lift, -(p[i + 1][1] - ny1) },
                              { p[i + 1][0] + nx1, p[i + 1][2] + lift, -(p[i + 1][1] + ny1) },
                              { p[i][0] + nx0, p[i][2] + lift, -(p[i][1] + ny0) } };
            quad(q[0], q[1], q[2], q[3], 0x565B66u, 0.92f);
        }
        if (dashes) {                                /* a bold dash over each piece's first half */
            float w0 = 0.22f / half, mx = (p[i][0] + p[i + 1][0]) / 2, my = (p[i][1] + p[i + 1][1]) / 2;
            float mh = (p[i][2] + p[i + 1][2]) / 2;
            float m[4][3] = { { p[i][0] - nx0 * w0, p[i][2] + lift + 0.08f, -(p[i][1] - ny0 * w0) },
                              { mx - nx0 * w0, mh + lift + 0.08f, -(my - ny0 * w0) },
                              { mx + nx0 * w0, mh + lift + 0.08f, -(my + ny0 * w0) },
                              { p[i][0] + nx0 * w0, p[i][2] + lift + 0.08f, -(p[i][1] + ny0 * w0) } };
            quad(m[0], m[1], m[2], m[3], 0xFFF4D0u, 1.0f);
        }
        along += l;
    }
}

/* ---- towns, depots, fields, trees ------------------------------------------------ */

static const uint32_t house_walls[] = { 0xF4E3C8u, 0xE8C9A0u, 0xF2F2F2u, 0xD8E8F0u, 0xF8D8D0u, 0xE0E8C0u };
static const uint32_t house_roofs[] = { 0xD8472Au, 0x3A6FD8u, 0x5A9A3Au, 0xE89A2Au, 0x8A4AB8u, 0x606878u };
static const uint32_t crops[] = { 0x5DBB3Fu, 0x52AD37u, 0x74C448u, 0xE6C84Au, 0xD9B040u, 0x9E7A4Au, 0x68B84Cu };

#define GRID 64.0f                               /* town block */

/* The street lines of town t in its own frame: -n..n blocks each way. */
static int town_blocks(const place *t)
{
    return (int)(t->r / GRID);
}

static void town_frame(const place *t, float u, float v, float *x, float *y)
{
    float c = cosf(t->heading), s = sinf(t->heading);
    *x = t->x + c * u - s * v;
    *y = t->y + s * u + c * v;
}

static void level_town(const place *t)
{
    int n = town_blocks(t), i;
    float h = ground(t->x, t->y);
    for (i = -n; i <= n; i++) {                   /* each street, level at the town's height */
        float x0, y0, x1, y1;
        town_frame(t, i * GRID, -n * GRID, &x0, &y0);
        town_frame(t, i * GRID, n * GRID, &x1, &y1);
        level_segment(x0, y0, h, x1, y1, h, 12.0f, 26.0f);
        town_frame(t, -n * GRID, i * GRID, &x0, &y0);
        town_frame(t, n * GRID, i * GRID, &x1, &y1);
        level_segment(x0, y0, h, x1, y1, h, 12.0f, 26.0f);
    }
}

static float road_distance(float x, float y)
{
    float best = 1e30f;
    int i, e;
    for (e = 0; e < nedges; e++)
        for (i = (int)edges[e].first_point; i < (int)(edges[e].first_point + edges[e].npoints); i++) {
            float dx = pts[i][0] - x, dy = pts[i][1] - y, d = dx * dx + dy * dy;
            if (d < best)
                best = d;
        }
    return sqrtf(best);
}

static void build_town(const place *t)
{
    int n = town_blocks(t), i, j, k;
    float h = ground(t->x, t->y);
    for (i = -n; i <= n; i++) {                   /* streets */
        float s[2][3], x0, y0, x1, y1;
        town_frame(t, i * GRID, -n * GRID, &x0, &y0);
        town_frame(t, i * GRID, n * GRID, &x1, &y1);
        s[0][0] = x0; s[0][1] = y0; s[0][2] = h; s[1][0] = x1; s[1][1] = y1; s[1][2] = h;
        road_ribbon((const float (*)[3])s, 2, 4.5f, 0, 0.12f);
        town_frame(t, -n * GRID, i * GRID, &x0, &y0);
        town_frame(t, n * GRID, i * GRID, &x1, &y1);
        s[0][0] = x0; s[0][1] = y0; s[1][0] = x1; s[1][1] = y1;
        road_ribbon((const float (*)[3])s, 2, 4.5f, 0, 0.13f);   /* crossings: not coplanar */
    }
    for (j = -n; j < n; j++)                      /* buildings, set back from the streets */
        for (i = -n; i < n; i++) {
            float bu = (i + 0.5f) * GRID, bv = (j + 0.5f) * GRID, rr = sqrtf(bu * bu + bv * bv);
            int count = 1 + (int)dgk_rng_below(&rng, 3);
            if (rr > t->r || frand() < 0.12f)
                continue;
            for (k = 0; k < count; k++) {
                float w = 9 + frand() * 10, d = 8 + frand() * 8, x, y;
                float ou = (count == 1 ? 0 : (k - (count - 1) / 2.0f) * 18.0f), ov = (frand() - 0.5f) * 10;
                float tall = 4 + frand() * (rr < t->r * 0.4f ? 8 : 4);    /* toy-low: the camera looks over them */
                uint32_t wall = house_walls[dgk_rng_below(&rng, 6)], roof = house_roofs[dgk_rng_below(&rng, 6)];
                town_frame(t, bu + ou, bv + ov, &x, &y);
                if (road_distance(x, y) < DGK_MAX(w, d) / 2 + 9.0f)          /* the main roads cut through */
                    continue;
                block(x, y, w / 2, d / 2, t->heading, tall, wall, roof, 1, 1);
            }
        }
}

static void depot_frame(const place *d, float u, float v, float *x, float *y)
{
    town_frame(d, u, v, x, y);
}

/* A depot: an apron 96 x 70 m, the warehouse along its back (+v) with
 * three dock bays, the entrance on the front (-v) where the road arrives. */
static void level_depot(const place *d)
{
    float h = ground(d->x, d->y), x0, y0, x1, y1;
    int k;
    for (k = -2; k <= 2; k++) {
        depot_frame(d, -48, k * 16.0f, &x0, &y0);
        depot_frame(d, 48, k * 16.0f, &x1, &y1);
        level_segment(x0, y0, h, x1, y1, h, 20.0f, 30.0f);
    }
}

static void build_depot(const place *d, wg_depot *out)
{
    float h = ground(d->x, d->y), c[4][2], x, y;
    static const float cu[4] = { -48, 48, 48, -48 }, cv[4] = { -35, -35, 35, 35 };
    int i;
    for (i = 0; i < 4; i++)
        depot_frame(d, cu[i], cv[i], &c[i][0], &c[i][1]);
    {
        float q[4][3];
        for (i = 0; i < 4; i++) {
            q[i][0] = c[i][0];
            q[i][1] = h + 0.12f;
            q[i][2] = -c[i][1];
        }
        quad(q[0], q[1], q[2], q[3], 0x8A8F99u, 0.95f);
    }
    depot_frame(d, 0, 45, &x, &y);                          /* the warehouse */
    block(x, y, 42, 10, d->heading, 7, 0xE4E0D6u, 0x3A6FD8u, 1, 1);
    for (i = 0; i < WG_BAYS; i++) {
        float bx, by, u = (i - 1) * 16.0f, px, py;
        depot_frame(d, u, 34.2f, &bx, &by);                 /* a dark door on the warehouse face */
        block(bx, by, 2.0f, 0.4f, d->heading, 4.2f, 0x2A2A33u, 0x2A2A33u, 0, 0);
        depot_frame(d, u - 7.5f, 31, &px, &py);             /* the dock between bays */
        block(px, py, 4.0f, 3.0f, d->heading, 1.3f, 0xC8C8D0u, 0xB0B0B8u, 0, 1);
        {                                                    /* bold bay lines */
            float lx0, ly0, lx1, ly1;
            depot_frame(d, u - 1.6f, 34, &lx0, &ly0);
            depot_frame(d, u - 1.6f, 14, &lx1, &ly1);
            ground_quad(lx0 - 0.15f, ly0, lx1 - 0.15f, ly1, lx1 + 0.15f, ly1, lx0 + 0.15f, ly0, 0.2f, 0xFFD23Fu, 1.0f);
            depot_frame(d, u + 1.6f, 34, &lx0, &ly0);
            depot_frame(d, u + 1.6f, 14, &lx1, &ly1);
            ground_quad(lx0 - 0.15f, ly0, lx1 - 0.15f, ly1, lx1 + 0.15f, ly1, lx0 + 0.15f, ly0, 0.2f, 0xFFD23Fu, 1.0f);
        }
        /* Docked: the trailer's rear at the dock face, pointing out of the bay. */
        depot_frame(d, u, 33.5f, &out->bay[i][0], &out->bay[i][1]);
        out->bay[i][2] = d->heading - WG_PI / 2;
    }
    depot_frame(d, 30, -12, &out->pickup[0], &out->pickup[1]);       /* a trailer waiting, nose to the west */
    out->pickup[2] = d->heading + WG_PI;
    out->x = d->x;
    out->y = d->y;
    out->heading = d->heading;
}

/* The ground: 16 m cells, each quad coloured by the field it lies in
 * (a patchwork of crops around Voronoi seeds), grass in towns. */
#define FIELDS 700
static float field_seed[FIELDS][2];
static uint8_t field_crop[FIELDS];

static uint32_t ground_colour(float x, float y)
{
    int i, best = 0;
    float bd = 1e30f;
    for (i = 0; i < nplaces; i++) {
        float dx = x - places[i].x, dy = y - places[i].y;
        if (dx * dx + dy * dy < (places[i].r + 30) * (places[i].r + 30))
            return 0x7CCB55u;                                   /* town and depot grass */
    }
    for (i = 0; i < FIELDS; i++) {
        float dx = x - field_seed[i][0], dy = y - field_seed[i][1], d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return crops[field_crop[best]];
}

static void build_terrain(void)
{
    int i, j;
    for (i = 0; i < FIELDS; i++) {
        field_seed[i][0] = frand() * WG_SIZE;
        field_seed[i][1] = frand() * WG_SIZE;
        field_crop[i] = (uint8_t)dgk_rng_below(&rng, DGK_ARRAY_LEN(crops));
    }
    for (j = 0; j < WG_HN - 1; j++)
        for (i = 0; i < WG_HN - 1; i++) {
            float x0 = i * WG_CELL, y0 = j * WG_CELL, x1 = x0 + WG_CELL, y1 = y0 + WG_CELL;
            float p[4][3] = { { x0, h_at(i, j), -y0 }, { x1, h_at(i + 1, j), -y0 },
                              { x1, h_at(i + 1, j + 1), -y1 }, { x0, h_at(i, j + 1), -y1 } };
            quad(p[0], p[1], p[2], p[3], ground_colour(x0 + WG_CELL / 2, y0 + WG_CELL / 2), -1);
        }
}

static void build_trees(void)
{
    static const uint32_t leaves[] = { 0x3E9E3Au, 0x4CB040u, 0x2F8A38u, 0x6AC24Au };
    int tries, placed = 0;
    for (tries = 0; tries < 60000 && placed < 5000; tries++) {
        float x = 20 + frand() * (WG_SIZE - 40), y = 20 + frand() * (WG_SIZE - 40), g;
        int i, near = 0;
        if (value_noise(x / 260.0f, y / 260.0f, 77) < 0.25f)       /* woods, not everywhere */
            continue;
        for (i = 0; i < nplaces && !near; i++) {
            float dx = x - places[i].x, dy = y - places[i].y;
            near = dx * dx + dy * dy < (places[i].r + 50) * (places[i].r + 50);
        }
        if (near || road_distance(x, y) < 18.0f)
            continue;
        g = ground(x, y);
        if (dgk_rng_below(&rng, 3)) {                               /* lollipop: trunk and a chunky crown */
            float s = 1.6f + frand() * 1.2f;
            block(x, y, 0.35f, 0.35f, 0, 2.2f + s * 0.4f, 0x7A5230u, 0x7A5230u, 0, 1);
            {
                float c0 = g + 1.8f, c1 = c0 + s * 2.2f;
                float v[8][3] = { { x - s, c0, -(y - s) }, { x + s, c0, -(y - s) }, { x + s, c0, -(y + s) },
                                  { x - s, c0, -(y + s) }, { x - s, c1, -(y - s) }, { x + s, c1, -(y - s) },
                                  { x + s, c1, -(y + s) }, { x - s, c1, -(y + s) } };
                uint32_t col = leaves[dgk_rng_below(&rng, 4)];
                quad(v[4], v[5], v[6], v[7], col, -1);
                quad(v[0], v[1], v[5], v[4], col, -1);
                quad(v[1], v[2], v[6], v[5], col, -1);
                quad(v[2], v[3], v[7], v[6], col, -1);
                quad(v[3], v[0], v[4], v[7], col, -1);
            }
        } else {                                                     /* cone: a four-sided spire */
            float s = 1.8f + frand() * 1.0f, top = g + 7 + frand() * 3;
            float v[5][3] = { { x - s, g + 0.6f, -(y - s) }, { x + s, g + 0.6f, -(y - s) }, { x + s, g + 0.6f, -(y + s) },
                              { x - s, g + 0.6f, -(y + s) }, { x, top, -y } };
            uint32_t col = leaves[dgk_rng_below(&rng, 4)];
            quad(v[0], v[1], v[4], v[4], col, -1);
            quad(v[1], v[2], v[4], v[4], col, -1);
            quad(v[2], v[3], v[4], v[4], col, -1);
            quad(v[3], v[0], v[4], v[4], col, -1);
            add_box(x, y, 0.5f, 0.5f, 0);
        }
        placed++;
    }
    printf("fwgen: %d trees\n", placed);
}

/* ---- the road network, the tour ------------------------------------------------ */

static void build_network(void)
{
    int in_tree[WG_MAX_PLACES] = { 0 }, i, j, k;
    for (i = 0; i < nplaces; i++) {
        if (places[i].kind == WG_DEPOT)
            depot_frame(&places[i], 0, -52, &nodes[i].x, &nodes[i].y);    /* the depot's gate */
        else {
            nodes[i].x = places[i].x;
            nodes[i].y = places[i].y;
        }
        nodes[i].kind = (uint32_t)places[i].kind;
        nodes[i].place = (uint32_t)i;
    }
    nnodes = nplaces;
    in_tree[0] = 1;                                  /* Prim's minimum spanning tree */
    for (k = 1; k < nnodes; k++) {
        int ba = -1, bb = -1;
        float bd = 1e30f;
        for (i = 0; i < nnodes; i++)
            for (j = 0; in_tree[i] && j < nnodes; j++)
                if (!in_tree[j] && dist2(i, j) < bd) {
                    bd = dist2(i, j);
                    ba = i;
                    bb = j;
                }
        in_tree[bb] = 1;
        add_road(ba, bb);
    }
    for (i = 0; i < nnodes; i++) {                   /* towns reach their two nearest towns: loops */
        int n1 = -1, n2 = -1;
        if (nodes[i].kind != WG_TOWN)
            continue;
        for (j = 0; j < nnodes; j++) {
            if (j == i || nodes[j].kind != WG_TOWN)
                continue;
            if (n1 < 0 || dist2(i, j) < dist2(i, n1)) {
                n2 = n1;
                n1 = j;
            } else if (n2 < 0 || dist2(i, j) < dist2(i, n2))
                n2 = j;
        }
        if (n1 >= 0 && !connected(i, n1))
            add_road(i, n1);
        if (n2 >= 0 && !connected(i, n2))
            add_road(i, n2);
    }
}

/* The longest simple cycle through the network (it is small), as the tour. */
static int best_cycle[64], best_len, path_nodes[64], path_edges[64], cycle_edges[64];
static float best_metres;

static void search(int at, int depth, float metres, int *used_node, int *used_edge)
{
    int e;
    for (e = 0; e < nedges; e++) {
        int other;
        if (used_edge[e] || ((int)edges[e].a != at && (int)edges[e].b != at))
            continue;
        other = (int)edges[e].a == at ? (int)edges[e].b : (int)edges[e].a;
        path_edges[depth] = e;
        if (other == path_nodes[0] && depth >= 2) {
            if (metres + edges[e].length > best_metres) {
                best_metres = metres + edges[e].length;
                best_len = depth + 1;
                memcpy(best_cycle, path_nodes, sizeof(int) * (size_t)best_len);
                memcpy(cycle_edges, path_edges, sizeof(int) * (size_t)best_len);
            }
            continue;
        }
        if (used_node[other] || nodes[other].kind == WG_DEPOT)   /* a depot is a dead end: no U-turns */
            continue;
        used_node[other] = used_edge[e] = 1;
        path_nodes[depth + 1] = other;
        search(other, depth + 1, metres + edges[e].length, used_node, used_edge);
        used_node[other] = used_edge[e] = 0;
    }
}

static float tour[MAX_POINTS][2];
static int ntour;

static void build_tour(void)
{
    int start, used_node[WG_MAX_PLACES], used_edge[MAX_EDGES], i, k;
    best_len = 0;
    for (start = 0; start < nnodes; start++) {
        if (nodes[start].kind != WG_TOWN)
            continue;
        memset(used_node, 0, sizeof used_node);
        memset(used_edge, 0, sizeof used_edge);
        used_node[start] = 1;
        path_nodes[0] = start;
        search(start, 0, 0, used_node, used_edge);
    }
    for (i = 0; i < best_len; i++) {                 /* the cycle's roads, each the right way round */
        const wg_edge *e = &edges[cycle_edges[i]];
        int forward = (int)e->a == best_cycle[i];
        for (k = 0; k + 1 < (int)e->npoints && ntour < MAX_POINTS; k++) {
            int p = (int)e->first_point + (forward ? k : (int)e->npoints - 1 - k);
            tour[ntour][0] = pts[p][0];
            tour[ntour][1] = pts[p][1];
            ntour++;
        }
    }
    printf("fwgen: tour of %d roads, %.0f m, %d points\n", best_len, best_metres, ntour);
}

/* ---- the pack ------------------------------------------------------------------ */

static void *grow(void *p, size_t *cap, size_t need)
{
    if (need > *cap) {
        while (*cap < need)
            *cap = *cap ? *cap * 2 : 65536;
        p = realloc(p, *cap);
    }
    return p;
}

static int write_pack(const char *path, const wg_depot *depots)
{
    pakw *w = pakw_new(seed_g, 0);
    wg_chunk *index = (wg_chunk *)calloc(WG_CHUNKS * WG_CHUNKS, sizeof *index);
    wg_vertex *verts = NULL;
    uint16_t *tris = NULL;
    size_t cap_v = 0, cap_t = 0, nv = 0, nt = 0;
    int c, i, n;
    for (c = 0; c < WG_CHUNKS * WG_CHUNKS; c++) {
        cmesh *m = &chunks[c];
        float ox = (c % WG_CHUNKS) * WG_CHUNK, oy = (c / WG_CHUNKS) * WG_CHUNK, hmin = 1e9f, hmax = -1e9f;
        index[c].first_vertex = (uint32_t)nv;
        index[c].nverts = (uint32_t)m->nverts;
        index[c].first_index = (uint32_t)nt * 3;
        index[c].ntris = (uint32_t)m->ntris;
        verts = (wg_vertex *)grow(verts, &cap_v, (nv + (size_t)m->nverts) * sizeof *verts);
        tris = (uint16_t *)grow(tris, &cap_t, (nt + (size_t)m->ntris) * 6);
        for (i = 0; i < m->nverts; i++) {
            wg_vertex *v = &verts[nv + (size_t)i];
            float x = m->pos[i * 3], h = m->pos[i * 3 + 1], y = -m->pos[i * 3 + 2];
            v->x = (int16_t)lrintf((x - ox) * WG_UNIT);
            v->y = (int16_t)lrintf(h * WG_UNIT);
            v->z = (int16_t)lrintf((oy - y) * WG_UNIT);
            v->pad = 0;
            memcpy(v->rgba, &m->rgba[i * 4], 4);
            hmin = DGK_MIN(hmin, h);
            hmax = DGK_MAX(hmax, h);
        }
        memcpy(tris + nt * 3, m->idx, (size_t)m->ntris * 6);
        index[c].hmin = (int16_t)floorf((m->nverts ? hmin : 0) * 10);
        index[c].hmax = (int16_t)ceilf((m->nverts ? hmax : 0) * 10);
        nv += (size_t)m->nverts;
        nt += (size_t)m->ntris;
    }
    {
        wg_world h;
        memset(&h, 0, sizeof h);
        h.version = 1; h.seed = seed_g; h.chunks = WG_CHUNKS; h.hn = WG_HN;
        h.size = WG_SIZE; h.chunk = WG_CHUNK; h.cell = WG_CELL; h.unit = WG_UNIT;
        pakw_section(w, WG_WORLD, &h, sizeof h);
    }
    pakw_section(w, WG_CINDEX, index, sizeof *index * WG_CHUNKS * WG_CHUNKS);
    pakw_section(w, WG_CVERTS, verts, (uint32_t)(nv * sizeof *verts));
    pakw_section(w, WG_CTRIS, tris, (uint32_t)(nt * 6));
    {
        int16_t *hg = (int16_t *)malloc(sizeof(int16_t) * WG_HN * WG_HN);
        for (i = 0; i < WG_HN * WG_HN; i++)
            hg[i] = (int16_t)lrintf(height[i] * 10);
        pakw_section(w, WG_HEIGHT, hg, sizeof(int16_t) * WG_HN * WG_HN);
        free(hg);
    }
    {
        uint8_t *b = (uint8_t *)malloc(4 + sizeof *boxes * (size_t)nboxes);
        uint32_t u = (uint32_t)nboxes;
        memcpy(b, &u, 4);
        memcpy(b + 4, boxes, sizeof *boxes * (size_t)nboxes);
        pakw_section(w, WG_BOXES, b, (uint32_t)(4 + sizeof *boxes * (size_t)nboxes));
        free(b);
    }
    {
        size_t size = sizeof(wg_graph) + sizeof(wg_node) * (size_t)nnodes + sizeof(wg_edge) * (size_t)nedges +
                      8 * (size_t)npts;
        uint8_t *g = (uint8_t *)malloc(size), *p = g;
        wg_graph hd;
        hd.nnodes = (uint32_t)nnodes; hd.nedges = (uint32_t)nedges; hd.npoints = (uint32_t)npts;
        memcpy(p, &hd, sizeof hd); p += sizeof hd;
        memcpy(p, nodes, sizeof(wg_node) * (size_t)nnodes); p += sizeof(wg_node) * (size_t)nnodes;
        memcpy(p, edges, sizeof(wg_edge) * (size_t)nedges); p += sizeof(wg_edge) * (size_t)nedges;
        for (i = 0; i < npts; i++, p += 8) {
            memcpy(p, &pts[i][0], 4);
            memcpy(p + 4, &pts[i][1], 4);
        }
        pakw_section(w, WG_GRAPH, g, (uint32_t)size);
        free(g);
    }
    {
        uint8_t *d = (uint8_t *)malloc(4 + sizeof(wg_depot) * (size_t)ndepots);
        uint32_t u = (uint32_t)ndepots;
        memcpy(d, &u, 4);
        memcpy(d + 4, depots, sizeof(wg_depot) * (size_t)ndepots);
        pakw_section(w, WG_DEPOTS, d, (uint32_t)(4 + sizeof(wg_depot) * (size_t)ndepots));
        free(d);
    }
    {
        uint8_t *t = (uint8_t *)malloc(4 + 8 * (size_t)ntour);
        uint32_t u = (uint32_t)ntour;
        float sp[3];
        memcpy(t, &u, 4);
        memcpy(t + 4, tour, 8 * (size_t)ntour);
        pakw_section(w, WG_TOUR, t, (uint32_t)(4 + 8 * (size_t)ntour));
        free(t);
        n = ntour > 3 ? 2 : 0;
        sp[0] = tour[n][0];
        sp[1] = tour[n][1];
        sp[2] = atan2f(tour[n + 1][1] - tour[n][1], tour[n + 1][0] - tour[n][0]);
        pakw_section(w, WG_SPAWN, sp, sizeof sp);
    }
    printf("fwgen: %lu vertices, %lu triangles in %d chunks, %d boxes, %d roads (%d points), %d depots\n",
           (unsigned long)nv, (unsigned long)nt, WG_CHUNKS * WG_CHUNKS, nboxes, nedges, npts, ndepots);
    free(index);
    free(verts);
    free(tris);
    return pakw_write(w, path);
}

int main(int argc, char **argv)
{
    static wg_depot depots[WG_MAX_PLACES];
    int i, e;
    FILE *f;
    if (argc != 3) {
        fprintf(stderr, "usage: fwgen SEED OUT.PAK\n");
        return 2;
    }
    seed_g = (uint32_t)strtoul(argv[1], NULL, 0);
    dgk_rng_seed(&rng, seed_g, 7);
    make_hills();
    place_kind(WG_TOWN, 5, 950.0f, 450.0f, 170.0f, 250.0f);
    ntowns = nplaces;
    place_kind(WG_DEPOT, 8, 560.0f, 320.0f, 60.0f, 60.0f);
    ndepots = nplaces - ntowns;
    for (i = 0; i < nplaces; i++)                    /* flat ground for towns and depots */
        if (places[i].kind == WG_TOWN)
            level_town(&places[i]);
        else
            level_depot(&places[i]);
    apply_levelling();
    memset(road_weight, 0, sizeof road_weight);
    build_network();
    for (e = 0; e < nedges; e++)                     /* the roads, levelled into the hills */
        for (i = (int)edges[e].first_point; i + 1 < (int)(edges[e].first_point + edges[e].npoints); i++)
            level_segment(pts[i][0], pts[i][1], pts[i][2], pts[i + 1][0], pts[i + 1][1], pts[i + 1][2], 12.0f, 28.0f);
    apply_levelling();
    build_terrain();
    for (e = 0; e < nedges; e++)
        road_ribbon((const float (*)[3])&pts[edges[e].first_point], (int)edges[e].npoints, 4.5f, 1,
                    0.16f + 0.02f * e);                     /* each road its own height: no z-fighting where they meet */
    for (i = 0; i < nplaces; i++)
        if (places[i].kind == WG_TOWN)
            build_town(&places[i]);
    for (i = ntowns; i < nplaces; i++) {
        float s[2][3];
        build_depot(&places[i], &depots[i - ntowns]);
        depots[i - ntowns].node = (uint32_t)i;
        depot_frame(&places[i], 0, -52, &s[0][0], &s[0][1]);        /* the gate to the apron */
        depot_frame(&places[i], 0, -34, &s[1][0], &s[1][1]);
        s[0][2] = s[1][2] = ground(places[i].x, places[i].y);
        road_ribbon((const float (*)[3])s, 2, 4.5f, 0, 0.14f);
    }
    build_trees();
    add_box(WG_SIZE / 2, -1, WG_SIZE / 2, 1, 0);          /* the world's edge */
    add_box(WG_SIZE / 2, WG_SIZE + 1, WG_SIZE / 2, 1, 0);
    add_box(-1, WG_SIZE / 2, WG_SIZE / 2, 1, WG_PI / 2);
    add_box(WG_SIZE + 1, WG_SIZE / 2, WG_SIZE / 2, 1, WG_PI / 2);
    build_tour();
    if (write_pack(argv[2], depots) != 0)
        return 1;
    f = fopen(argv[2], "rb");
    if (f) {
        static uint8_t buf[1 << 16];
        uint32_t crc = 0;
        size_t n;
        long total = 0;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            crc = dgk_crc32(crc, buf, n);
            total += (long)n;
        }
        fclose(f);
        printf("fwgen: %s %ld bytes, hash %08lx\n", argv[2], total, (unsigned long)crc);
    }
    return 0;
}
