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

/* The terrain as drawn: each cell split into the triangles (i, j)-(i+1, j)-(i+1, j+1)
 * and (i, j)-(i+1, j+1)-(i, j+1), as the game's world_height reads it. The
 * three samples of the triangle under (x, y), and its height there. */
static float terrain_tri(float x, float y, int idx[3])
{
    float gx = x / WG_CELL, gy = y / WG_CELL, fx, fy;
    int i = DGK_CLAMP((int)floorf(gx), 0, WG_HN - 2), j = DGK_CLAMP((int)floorf(gy), 0, WG_HN - 2);
    fx = gx - i;
    fy = gy - j;
    idx[0] = j * WG_HN + i;
    if (fx >= fy) {
        idx[1] = j * WG_HN + i + 1;
        idx[2] = (j + 1) * WG_HN + i + 1;
        return height[idx[0]] * (1 - fx) + height[idx[1]] * (fx - fy) + height[idx[2]] * fy;
    }
    idx[1] = (j + 1) * WG_HN + i + 1;
    idx[2] = (j + 1) * WG_HN + i;
    return height[idx[0]] * (1 - fy) + height[idx[1]] * fx + height[idx[2]] * (fy - fx);
}

/* What lies flat on the ground (roads, streets, aprons) is drawn a little
 * above its own height; the terrain under it is kept at least CLEARANCE below
 * that height, and (filled) no more than FILL below it. */
#define CLEARANCE 0.02f
#define FILL 0.3f

/* Fit the terrain under a strip from (x0, y0) at height h0 to (x1, y1) at
 * h1, `half` wide either side plus half a metre (for a ribbon's mitred
 * joints), to the strip's plane (its grade carried on past its ends, level
 * across it). Cutting, each terrain triangle under the strip has its corners
 * lowered to the plane less the clearance: a triangle and the plane are both
 * flat, so the triangle then lies below the strip wherever they overlap.
 * Filling, they are raised to the plane less FILL, so the strip does not
 * stand far above the ground (the lorry rides the ground). Levelling
 * flattens the samples near a road, but a 16 m cell's far corners could keep
 * a crest's height and rise through the road between them, or a hollow's and
 * leave it standing in the air. The triangles are found at points a metre
 * apart along the strip and 1.25 m or less across. */
static void fit_strip(float x0, float y0, float h0, float x1, float y1, float h1, float half, int fill)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx, ny, r = half + 0.5f;
    int n, i, k, c, idx[3], across = (int)ceilf(r / 1.25f);
    if (l < 0.01f)
        return;
    nx = -dy / l;
    ny = dx / l;
    n = (int)ceilf(l);
    for (i = 0; i <= n; i++)
        for (k = -across; k <= across; k++) {
            float t = (float)i / n, o = r * k / across;
            terrain_tri(x0 + dx * t + nx * o, y0 + dy * t + ny * o, idx);
            for (c = 0; c < 3; c++) {
                float px = (float)(idx[c] % WG_HN) * WG_CELL - x0, py = (float)(idx[c] / WG_HN) * WG_CELL - y0;
                float plane = h0 + (h1 - h0) * (px * dx + py * dy) / (l * l);
                if (fill && height[idx[c]] < plane - FILL)
                    height[idx[c]] = plane - FILL;
                else if (!fill && height[idx[c]] > plane - CLEARANCE)
                    height[idx[c]] = plane - CLEARANCE;
            }
        }
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
    float h;                                      /* its streets or apron: the ground there, levelled */
    int kind;                                     /* WG_TOWN, WG_DEPOT */
} place;

#define GRID 64.0f                               /* town block */
/* A town is flat to TOWN_FLAT beyond its outer streets (every block too: a
 * block's middle is 32 m from a street), easing back into the hills over
 * TOWN_EASE (wide enough that no slope at a town's edge passes 25 degrees). */
#define TOWN_FLAT 36.0f
#define TOWN_EASE 56.0f
/* A road on a place's flat ground is at the place's height, easing back to
 * its own profile over this many metres of road beyond it. */
#define ROAD_EASE 120.0f

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
    float *uv;                                    /* the atlas's s, t (wgen.h; the look test) */
    uint8_t *rgba;
    uint16_t *idx;
    int nverts, ntris, cap_v, cap_t;
} cmesh;

static cmesh chunks[WG_VARIANTS][WG_CHUNKS * WG_CHUNKS];
static int variant = WG_SHARED;                   /* where quad() puts what it makes */
/* HIGH's textures: the atlas tile the next vertices map (WG_TX_PLAIN: its middle, no pattern) and where in it
 * (0..1); tex_quad: quad() and face() map their corners over the whole tile. In WORLDHI.PAK only (write_hi). */
static int tex_tile = WG_TX_PLAIN, tex_quad;
static uint32_t main_verts, main_tris;            /* WORLD.PAK's, for WORLDHI.PAK's check */
static float tex_u = 0.5f, tex_v = 0.5f;
static long total_tris;

static cmesh *chunk_for(float x, float y)
{
    int cx = (int)(x / WG_CHUNK), cy = (int)(y / WG_CHUNK);
    cx = cx < 0 ? 0 : cx >= WG_CHUNKS ? WG_CHUNKS - 1 : cx;
    cy = cy < 0 ? 0 : cy >= WG_CHUNKS ? WG_CHUNKS - 1 : cy;
    return &chunks[variant][cy * WG_CHUNKS + cx];
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
        c->uv = (float *)realloc(c->uv, (size_t)c->cap_v * 8);
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
    {
        static const float cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
        int k;
        for (k = c->nverts; k < b.nverts; k++) {
            int corner = (k - c->nverts) & 3, plain = tex_tile == WG_TX_PLAIN || !tex_quad;
            wg_tile_uv(tex_tile, plain ? 0.5f : cu[corner], plain ? 0.5f : cv[corner], &c->uv[k * 2], &c->uv[k * 2 + 1]);
        }
    }
    c->nverts = b.nverts;
    c->ntris = b.ntris;
    total_tris += 2;
}

/* A vertex of chunk c's mesh (GL coordinates, an RGB colour): its index. */
static int cmesh_vertex(cmesh *c, const float *p, uint32_t rgb)
{
    if (c->nverts + 1 > c->cap_v) {
        c->cap_v = c->cap_v ? c->cap_v * 2 : 1024;
        c->pos = (float *)realloc(c->pos, (size_t)c->cap_v * 12);
        c->uv = (float *)realloc(c->uv, (size_t)c->cap_v * 8);
        c->rgba = (uint8_t *)realloc(c->rgba, (size_t)c->cap_v * 4);
    }
    memcpy(&c->pos[c->nverts * 3], p, 3 * sizeof(float));
    wg_tile_uv(tex_tile, tex_tile == WG_TX_PLAIN ? 0.5f : tex_u, tex_tile == WG_TX_PLAIN ? 0.5f : tex_v,
               &c->uv[c->nverts * 2], &c->uv[c->nverts * 2 + 1]);
    c->rgba[c->nverts * 4 + 0] = (uint8_t)(rgb >> 16);
    c->rgba[c->nverts * 4 + 1] = (uint8_t)(rgb >> 8);
    c->rgba[c->nverts * 4 + 2] = (uint8_t)rgb;
    c->rgba[c->nverts * 4 + 3] = 255;
    return c->nverts++;
}

static void cmesh_tri(cmesh *c, int a, int b, int d)
{
    if (c->ntris + 1 > c->cap_t) {
        c->cap_t = c->cap_t ? c->cap_t * 2 : 1024;
        c->idx = (uint16_t *)realloc(c->idx, (size_t)c->cap_t * 6);
    }
    c->idx[c->ntris * 3 + 0] = (uint16_t)a;
    c->idx[c->ntris * 3 + 1] = (uint16_t)b;
    c->idx[c->ntris * 3 + 2] = (uint16_t)d;
    c->ntris++;
    total_tris++;
}

/* ---- smooth shapes (MEDIUM and HIGH's run) ------------------------------------- */

/* A vertex lit by the sun on its own normal n, darker towards the ground
 * (shade: 1 at the top of the shape, `low` at its foot, by its normal's
 * height): a baked hint of the shadow under a crown. */
static int smooth_vertex(cmesh *c, const float *p, const float *n, uint32_t rgb, float low)
{
    float k = low + (1.0f - low) * (0.5f + 0.5f * n[1]);
    return cmesh_vertex(c, p, mb_shade(rgb, mb_light(n[0], n[1], n[2]) * k));
}

/* A round blob: an icosahedron's 20 faces, smooth-shaded, centre (x, y)
 * on the ground plane at height z, radii rx (across) and rz (up). */
static void blob(float x, float y, float z, float rx, float rz, uint32_t rgb)
{
    static const float ico[12][3] = {
        { -0.526f, 0.851f, 0 }, { 0.526f, 0.851f, 0 }, { -0.526f, -0.851f, 0 }, { 0.526f, -0.851f, 0 },
        { 0, -0.526f, 0.851f }, { 0, 0.526f, 0.851f }, { 0, -0.526f, -0.851f }, { 0, 0.526f, -0.851f },
        { 0.851f, 0, -0.526f }, { 0.851f, 0, 0.526f }, { -0.851f, 0, -0.526f }, { -0.851f, 0, 0.526f } };
    static const int face[20][3] = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
        { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 },
        { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    cmesh *c = chunk_for(x, y);
    int v[12], i, save = tex_tile;
    tex_tile = WG_TX_LEAVES;
    for (i = 0; i < 12; i++) {
        /* The icosahedron's y is up; its x and z lie on the ground (GL z is -y). */
        float p[3], n[3], l;
        p[0] = x + ico[i][0] * rx;
        p[1] = z + ico[i][1] * rz;
        p[2] = -y + ico[i][2] * rx;
        n[0] = ico[i][0] / rx;
        n[1] = ico[i][1] / rz;
        n[2] = ico[i][2] / rx;
        l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        n[0] /= l;
        n[1] /= l;
        n[2] /= l;
        tex_u = 0.5f + ico[i][0] * 0.5f;
        tex_v = 0.5f + ico[i][2] * 0.5f;
        v[i] = smooth_vertex(c, p, n, rgb, 0.7f);
    }
    for (i = 0; i < 20; i++)
        cmesh_tri(c, v[face[i][0]], v[face[i][1]], v[face[i][2]]);
    tex_tile = save;
}

/* A smooth cone round (x, y): base radius r at height z0, its tip at z1, n
 * sides; no base (it stands on something). */
static void cone(float x, float y, float z0, float z1, float r, int n, uint32_t rgb)
{
    cmesh *c = chunk_for(x, y);
    float slope = r / (z1 - z0), l = sqrtf(1 + slope * slope);
    int ring[12], i, tip;
    float p[3], nm[3] = { 0, 1, 0 };
    for (i = 0; i < n; i++) {
        float a = 6.2831853f * i / n;
        p[0] = x + r * cosf(a);
        p[1] = z0;
        p[2] = -y + r * sinf(a);
        nm[0] = cosf(a) / l;
        nm[1] = slope / l;
        nm[2] = sinf(a) / l;
        ring[i] = smooth_vertex(c, p, nm, rgb, 0.72f);
    }
    p[0] = x; p[1] = z1; p[2] = -y;
    nm[0] = 0; nm[1] = 1; nm[2] = 0;
    tip = smooth_vertex(c, p, nm, rgb, 0.72f);
    for (i = 0; i < n; i++)
        cmesh_tri(c, ring[(i + 1) % n], ring[i], tip);
}

/* A trunk: a cylinder's n sides (no ends), round (x, y) from z0 to z1. */
static void trunk(float x, float y, float z0, float z1, float r, int n, uint32_t rgb)
{
    cmesh *c = chunk_for(x, y);
    int lo[12], hi[12], i;
    for (i = 0; i < n; i++) {
        float a = 6.2831853f * i / n, p[3], nm[3];
        nm[0] = cosf(a); nm[1] = 0; nm[2] = sinf(a);
        p[0] = x + r * nm[0]; p[1] = z0; p[2] = -y + r * nm[2];
        lo[i] = smooth_vertex(c, p, nm, rgb, 1.0f);
        p[1] = z1;
        hi[i] = smooth_vertex(c, p, nm, rgb, 1.0f);
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        cmesh_tri(c, lo[j], lo[i], hi[i]);
        cmesh_tri(c, lo[j], hi[i], hi[j]);
    }
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

/* ---- houses (MEDIUM and HIGH's run) ------------------------------------------------ */

static uint32_t ground_colour(float x, float y);

/* A building's frame: (u, v) along and across `angle` from its centre. */
typedef struct bframe {
    float cx, cy, c, s;
} bframe;

static void bpoint(const bframe *f, float u, float v, float h, float *p)
{
    p[0] = f->cx + f->c * u - f->s * v;
    p[1] = h;
    p[2] = -(f->cy + f->s * u + f->c * v);
}

/* A flat face of n (3 or 4) points, turned to face `out` (GL), lit by its
 * normal; points below `foot` darker towards it (a baked hint of the shade
 * where a wall meets the ground). */
static void face(cmesh *m, float (*p)[3], int n, const float *out, uint32_t rgb, float foot)
{
    float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
    float vx = p[n - 1][0] - p[0][0], vy = p[n - 1][1] - p[0][1], vz = p[n - 1][2] - p[0][2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, l, light;
    int v[4], i, flip;
    l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l < 1e-6f)
        return;
    flip = nx * out[0] + ny * out[1] + nz * out[2] < 0;
    if (flip) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    light = mb_light(nx / l, ny / l, nz / l);
    for (i = 0; i < n; i++) {
        static const float cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 }, tu[3] = { 0, 1, 0.5f }, tv[3] = { 0, 0, 1 };
        float k = p[i][1] < foot + 0.05f ? 0.8f : 1.0f;
        if (tex_quad) {
            tex_u = n == 3 ? tu[i] : cu[i & 3];
            tex_v = n == 3 ? tv[i] : cv[i & 3];
        }
        v[i] = cmesh_vertex(m, p[i], mb_shade(rgb, light * k));
    }
    for (i = 1; i + 1 < n; i++)
        if (flip)
            cmesh_tri(m, v[0], v[i + 1], v[i]);
        else
            cmesh_tri(m, v[0], v[i], v[i + 1]);
}

/* A wall panel on the side of a building facing (du, dv) (a unit
 * direction in its frame), from u0 to u1 along it (as seen from outside),
 * at distance d out, from height h0 to h1. */
static void panel(cmesh *m, const bframe *f, float du, float dv, float d, float a0, float a1, float h0, float h1,
                  uint32_t rgb, float foot)
{
    float p[4][3], out[3];
    /* Along the side: perpendicular to (du, dv). */
    float su = -dv, sv = du;
    bpoint(f, du * d + su * a0, dv * d + sv * a0, h0, p[0]);
    bpoint(f, du * d + su * a1, dv * d + sv * a1, h0, p[1]);
    bpoint(f, du * d + su * a1, dv * d + sv * a1, h1, p[2]);
    bpoint(f, du * d + su * a0, dv * d + sv * a0, h1, p[3]);
    out[0] = f->c * du - f->s * dv;
    out[1] = 0;
    out[2] = -(f->s * du + f->c * dv);
    face(m, p, 4, out, rgb, foot);
}

/* The building's base on sloping ground: its corners' lowest, sunk a little. */
static float bbase(const bframe *f, float hl, float hw)
{
    float base = ground(f->cx, f->cy), p[3];
    int i;
    for (i = 0; i < 4; i++) {
        bpoint(f, (i & 1) ? hl : -hl, (i & 2) ? hw : -hw, 0, p);
        base = DGK_MIN(base, ground(p[0], -p[2]));
    }
    return base;
}

/* Its walls (a box, open below) from `base` - 0.5 to `top`, the four sides darker at their feet. */
static void walls(cmesh *m, const bframe *f, float hl, float hw, float base, float top, uint32_t rgb)
{
    panel(m, f, 0, -1, hw, -hl, hl, base - 0.5f, top, rgb, base);
    panel(m, f, 1, 0, hl, -hw, hw, base - 0.5f, top, rgb, base);
    panel(m, f, 0, 1, hw, -hl, hl, base - 0.5f, top, rgb, base);
    panel(m, f, -1, 0, hl, -hw, hw, base - 0.5f, top, rgb, base);
}

/* A contact shadow to the south-east, in the ground's own colour, darker. */
static void bshadow(const bframe *f, float hl, float hw, float h)
{
    float off = h * 0.35f, k[4][3];
    int i;
    for (i = 0; i < 4; i++)
        bpoint(f, (i == 1 || i == 2) ? hl : -hl, i >= 2 ? hw : -hw, 0, k[i]);
    ground_quad(k[0][0] + off, -k[0][2] - off, k[1][0] + off, -k[1][2] - off, k[2][0] + off, -k[2][2] - off,
                k[3][0] + off, -k[3][2] - off, 0.08f, ground_colour(f->cx, f->cy), 0.6f);
}

/* A depot's warehouse for MEDIUM and HIGH, on block()'s footprint: walls,
 * and an arched roof (smooth, eight strips) over its length. */
static void warehouse(float cx, float cy, float hl, float hw, float angle, float h, uint32_t wall, uint32_t roof)
{
    enum { ARCH = 8 };
    bframe f;
    cmesh *m;
    float base, eave, rise = 2.6f, o = 0.5f;
    int i, e, ring[2][ARCH + 1];
    f.cx = cx; f.cy = cy; f.c = cosf(angle); f.s = sinf(angle);
    m = chunk_for(cx, cy);
    base = bbase(&f, hl, hw);
    eave = base + h;
    bshadow(&f, hl, hw, h + rise);
    walls(m, &f, hl, hw, base, eave, wall);
    for (e = 0; e < 2; e++)                          /* the roof: a parabola across, along u */
        for (i = 0; i <= ARCH; i++) {
            float t = -1.0f + 2.0f * i / ARCH, v = t * (hw + o), y = eave + rise * (1 - t * t) - (t * t) * 0.3f, p[3];
            float slope = -2.0f * rise * t / (hw + o), nl = sqrtf(1 + slope * slope), n[3];
            bpoint(&f, e ? hl + 0.4f : -hl - 0.4f, v, y, p);
            /* The normal across the arch, in GL: (-slope, 1) turned by the building's angle. */
            n[0] = (f.s * slope) / nl;
            n[1] = 1 / nl;
            n[2] = (f.c * slope) / nl;
            ring[e][i] = smooth_vertex(m, p, n, roof, 1.0f);
        }
    for (i = 0; i < ARCH; i++) {
        cmesh_tri(m, ring[0][i], ring[1][i], ring[1][i + 1]);
        cmesh_tri(m, ring[0][i], ring[1][i + 1], ring[0][i + 1]);
    }
    for (e = -1; e <= 1; e += 2) {                   /* the ends, under the arch: a fan in the walls' colour */
        float p[3][3], out[3];
        out[0] = f.c * e;
        out[1] = 0;
        out[2] = -f.s * e;
        for (i = 0; i < ARCH; i++) {
            float t0 = -1.0f + 2.0f * i / ARCH, t1 = -1.0f + 2.0f * (i + 1) / ARCH;
            bpoint(&f, e * hl, 0, eave, p[0]);
            bpoint(&f, e * hl, t0 * hw, eave + rise * (1 - t0 * t0), p[1]);
            bpoint(&f, e * hl, t1 * hw, eave + rise * (1 - t1 * t1), p[2]);
            face(m, p, 3, out, wall, -1e9f);
        }
    }
}

/* ---- painted digits ------------------------------------------------------------- */

/* The digits of data/font5x7.txt (the HUD's font), copied: '#' ink. */
static const char *const digit_rows[10][7] = {
    { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." },
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." },
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" },
    { "####.", "....#", "....#", ".###.", "....#", "....#", "####." },
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." },
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." },
    { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." },
    { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." },
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." },
    { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." }
};

/* Paint the number n's digits: each row's runs of ink a quad, `px` metres a
 * pixel, the glyphs' left top at `o` (GL), along `right` and down `down`
 * (unit GL vectors), facing `out`, in colour rgb; a pixel's gap between
 * digits. */
static void paint_number(int n, const float *o, const float *right, const float *down, const float *out,
                         float px, uint32_t rgb)
{
    char s[8];
    int len = snprintf(s, sizeof s, "%d", n), k, r, c;
    cmesh *m;
    float cx = o[0] + right[0] * len * 3 * px, cz = o[2] + right[2] * len * 3 * px;
    m = chunk_for(cx, -cz);
    for (k = 0; k < len; k++)
        for (r = 0; r < 7; r++)
            for (c = 0; c < 5; c++) {
                const char *row = digit_rows[s[k] - '0'][r];
                int e, i;
                float p[4][3], a0, a1;
                if (row[c] != '#' || (c > 0 && row[c - 1] == '#'))
                    continue;
                for (e = c; e < 5 && row[e] == '#'; e++)
                    continue;
                a0 = (k * 6 + c) * px;
                a1 = (k * 6 + e) * px;
                for (i = 0; i < 3; i++) {
                    p[0][i] = o[i] + right[i] * a0 + down[i] * r * px;
                    p[1][i] = o[i] + right[i] * a1 + down[i] * r * px;
                    p[2][i] = o[i] + right[i] * a1 + down[i] * (r + 1) * px;
                    p[3][i] = o[i] + right[i] * a0 + down[i] * (r + 1) * px;
                }
                face(m, p, 4, out, rgb, -1e9f);
            }
}

/* ---- roads ------------------------------------------------------------------------ */

#define MAX_EDGES 64
#define MAX_POINTS 20000
static wg_node nodes[WG_MAX_NODES];
static float node_h[WG_MAX_NODES];                /* a junction's height: its roads meet there level */
static wg_edge edges[MAX_EDGES];
static int nnodes, nedges;
static float pts[MAX_POINTS][3];                 /* x, y, road height */
static int npts;

static float dist2(int a, int b)
{
    float dx = nodes[a].x - nodes[b].x, dy = nodes[a].y - nodes[b].y;
    return dx * dx + dy * dy;
}

/* Inside a depot's yard (the apron and the warehouse, with a margin for
 * the road's width)? */
static int in_depot(float x, float y)
{
    int i;
    for (i = 0; i < nplaces; i++) {
        const place *d = &places[i];
        float c = cosf(d->heading), s = sinf(d->heading), dx = x - d->x, dy = y - d->y;
        float u = dx * c + dy * s, v = -dx * s + dy * c;
        if (d->kind == WG_DEPOT && fabsf(u) < 56.0f && v > -44.0f && v < 62.0f)
            return 1;
    }
    return 0;
}

/* On a place's flat ground (level_town, level_depot): a town's to TOWN_FLAT
 * beyond its outer streets; a depot's apron, its gate (on the edge) and the
 * yard around it. */
static int place_flat(const place *p, float x, float y)
{
    float c = cosf(p->heading), s = sinf(p->heading), dx = x - p->x, dy = y - p->y;
    float u = dx * c + dy * s, v = -dx * s + dy * c;
    if (p->kind == WG_TOWN) {
        float e = (float)(int)(p->r / GRID) * GRID + TOWN_FLAT;
        return fabsf(u) <= e && fabsf(v) <= e;
    }
    return fabsf(u) <= 68.0f && fabsf(v) <= 53.0f;
}

/* Where a road leaves a depot: 50 m straight out from its gate. */
static void depot_stub(int node, float *x, float *y)
{
    const place *d = &places[node];
    *x = d->x + sinf(d->heading) * 102.0f;
    *y = d->y - cosf(d->heading) * 102.0f;
}

static int road_attempt(int a, int b, int attempt);

/* A road from node a to b that keeps out of every depot's yard: tried with
 * fresh, wider jitter until it does. */
static int crossing_roads;

static void add_road(int a, int b)
{
    int attempt;
    for (attempt = 0; attempt < 24; attempt++)
        if (road_attempt(a, b, attempt))
            return;
    road_attempt(a, b, -1);
    fprintf(stderr, "fwgen: the road from place %d to place %d crosses a depot\n", a, b);
    crossing_roads++;
}

/* Where the segment from p (inside) to q (outside) crosses the circle of
 * radius r about c. */
static void cross_circle(const float *c, const float *p, const float *q, float r, float *out)
{
    float dx = q[0] - p[0], dy = q[1] - p[1], fx = p[0] - c[0], fy = p[1] - c[1];
    float A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - r * r;
    float t = (-B + sqrtf(DGK_MAX(B * B - 4 * A * C, 0.0f))) / (2 * A);
    out[0] = p[0] + dx * t;
    out[1] = p[1] + dy * t;
}

/* A road's points from or to a town stop at its ring: those inside go, a
 * point where it crosses the ring comes first (or last), and a point
 * within 3 m of that one goes too. Returns the count left. */
static int trim_at_rings(int first, int count, int a, int b)
{
    float c[2], hit[2];
    int k, end;
    for (end = 0; end < 2; end++) {
        int nd = end ? b : a;
        if (nodes[nd].kind != WG_TOWN || count < 4)
            continue;
        c[0] = nodes[nd].x;
        c[1] = nodes[nd].y;
        if (end)                                     /* reverse, trim the front, reverse back */
            for (k = 0; k < count / 2; k++) {
                float t0 = pts[first + k][0], t1 = pts[first + k][1];
                pts[first + k][0] = pts[first + count - 1 - k][0];
                pts[first + k][1] = pts[first + count - 1 - k][1];
                pts[first + count - 1 - k][0] = t0;
                pts[first + count - 1 - k][1] = t1;
            }
        for (k = 0; k < count; k++) {
            float dx = pts[first + k][0] - c[0], dy = pts[first + k][1] - c[1];
            if (dx * dx + dy * dy >= WG_RING_R * WG_RING_R)
                break;
        }
        if (k > 0 && k < count) {
            int keep;
            cross_circle(c, pts[first + k - 1], pts[first + k], WG_RING_R, hit);
            keep = (pts[first + k][0] - hit[0]) * (pts[first + k][0] - hit[0]) +
                   (pts[first + k][1] - hit[1]) * (pts[first + k][1] - hit[1]) < 9.0f ? k + 1 : k;
            memmove(&pts[first + 1], &pts[first + keep], sizeof pts[0] * (size_t)(count - keep));
            count -= keep - 1;
            pts[first][0] = hit[0];
            pts[first][1] = hit[1];
        }
        if (end)
            for (k = 0; k < count / 2; k++) {
                float t0 = pts[first + k][0], t1 = pts[first + k][1];
                pts[first + k][0] = pts[first + count - 1 - k][0];
                pts[first + k][1] = pts[first + count - 1 - k][1];
                pts[first + count - 1 - k][0] = t0;
                pts[first + count - 1 - k][1] = t1;
            }
    }
    return count;
}

/* One try at a road from node a to b: a curve through jittered points (out
 * of a depot along its stub), sampled about every 8 m; its
 * height profile the ground's, smoothed. Returns 0 (and takes it back) if
 * it crosses a depot, unless attempt is -1 (keep it anyway). */
static int road_attempt(int a, int b, int attempt)
{
    float cp[7][2], len = sqrtf(dist2(a, b)), nx, ny, raw[4096];
    float spread = 1.0f + (attempt < 0 ? 0 : attempt) * 0.2f;
    int n = 0, i, k, seg, first = npts, count, end;
    wg_edge *e = &edges[nedges];
    if (nedges == MAX_EDGES)
        return 1;
    nx = -(nodes[b].y - nodes[a].y) / len;
    ny = (nodes[b].x - nodes[a].x) / len;
    cp[0][0] = nodes[a].x; cp[0][1] = nodes[a].y;
    for (i = 1; i <= 4; i++) {
        float t = i / 5.0f, j = (frand() - 0.5f) * DGK_MIN(0.22f * len, 260.0f) * spread;
        cp[i][0] = DGK_CLAMP(nodes[a].x + (nodes[b].x - nodes[a].x) * t + nx * j, 60.0f, WG_SIZE - 60.0f);
        cp[i][1] = DGK_CLAMP(nodes[a].y + (nodes[b].y - nodes[a].y) * t + ny * j, 60.0f, WG_SIZE - 60.0f);
    }
    cp[5][0] = nodes[b].x; cp[5][1] = nodes[b].y;
    if (nodes[a].kind == WG_DEPOT)
        depot_stub(a, &cp[1][0], &cp[1][1]);
    if (nodes[b].kind == WG_DEPOT)
        depot_stub(b, &cp[4][0], &cp[4][1]);
    for (seg = 0; seg < 5; seg++) {
        /* Hermite segments with Catmull-Rom tangents, except at a depot:
         * the stub is straight, and the curve meets it in line. */
        const float *p0 = cp[seg > 0 ? seg - 1 : 0], *p1 = cp[seg], *p2 = cp[seg + 1], *p3 = cp[seg < 4 ? seg + 2 : 5];
        float sl = sqrtf((p2[0] - p1[0]) * (p2[0] - p1[0]) + (p2[1] - p1[1]) * (p2[1] - p1[1]));
        float m1[2] = { (p2[0] - p0[0]) * 0.5f, (p2[1] - p0[1]) * 0.5f };
        float m2[2] = { (p3[0] - p1[0]) * 0.5f, (p3[1] - p1[1]) * 0.5f };
        int steps = (int)(sl / 8.0f) + 1;
        if ((seg == 0 && nodes[a].kind == WG_DEPOT) || (seg == 4 && nodes[b].kind == WG_DEPOT)) {
            m1[0] = m2[0] = p2[0] - p1[0];
            m1[1] = m2[1] = p2[1] - p1[1];
        }
        if (seg == 1 && nodes[a].kind == WG_DEPOT) {        /* leave the stub in line */
            float dx = cp[1][0] - cp[0][0], dy = cp[1][1] - cp[0][1], dl = sqrtf(dx * dx + dy * dy);
            m1[0] = (cp[1][0] - cp[0][0]) / dl * sl;
            m1[1] = (cp[1][1] - cp[0][1]) / dl * sl;
        }
        if (seg == 3 && nodes[b].kind == WG_DEPOT) {        /* reach it in line */
            float dx = cp[5][0] - cp[4][0], dy = cp[5][1] - cp[4][1], dl = sqrtf(dx * dx + dy * dy);
            m2[0] = (cp[5][0] - cp[4][0]) / dl * sl;
            m2[1] = (cp[5][1] - cp[4][1]) / dl * sl;
        }
        for (k = 0; k < steps && npts < MAX_POINTS && n < 4096; k++) {
            float t = (float)k / steps, t2 = t * t, t3 = t2 * t;
            float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            float x = h00 * p1[0] + h10 * m1[0] + h01 * p2[0] + h11 * m2[0];
            float y = h00 * p1[1] + h10 * m1[1] + h01 * p2[1] + h11 * m2[1];
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
    count = trim_at_rings(first, npts - first, a, b);
    npts = first + count;
    for (i = 0; i < count; i++)
        raw[i] = ground(pts[first + i][0], pts[first + i][1]);
    if (attempt >= 0)
        for (i = 0; i < count; i++)
            if (in_depot(pts[first + i][0], pts[first + i][1])) {
                npts = first;
                return 0;
            }
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
    /* On a town's or a depot's flat ground the road is at the place's height,
     * easing back to its own profile beyond: roads meet in a town at one
     * height and reach a depot's gate at its apron's. */
    for (end = 0; end < 2; end++) {
        /* A place's flat ground, or (a junction) the first 10 m from it, at one height. */
        int nd = end ? b : a;
        const place *pl = nd < nplaces ? &places[nd] : NULL;
        float along = 0, last_on = -1, flat_h = pl ? pl->h : node_h[nd];
        for (k = 0; k < count; k++) {
            int q = end ? count - 1 - k : k, prev = end ? q + 1 : q - 1;
            float w;
            if (k)
                along += sqrtf((pts[first + q][0] - pts[first + prev][0]) * (pts[first + q][0] - pts[first + prev][0]) +
                               (pts[first + q][1] - pts[first + prev][1]) * (pts[first + q][1] - pts[first + prev][1]));
            if (pl ? place_flat(pl, pts[first + q][0], pts[first + q][1]) : along <= 10.0f) {
                raw[q] = flat_h;
                last_on = along;
                continue;
            }
            if (last_on < 0 || along - last_on >= ROAD_EASE)
                break;
            w = 1.0f - smooth((along - last_on) / ROAD_EASE);
            raw[q] += (flat_h - raw[q]) * w;
        }
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
    return 1;
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
/* A road's painting: dashes down the middle, and (MEDIUM and HIGH's run)
 * a white line along each edge; none within quiet0 metres of its start or
 * quiet1 of its end, where it meets other roads in a town's middle. */
typedef struct paint {
    int dashes, edges;
    float quiet0, quiet1;
} paint;

static void road_ribbon(const float (*p)[3], int n, float half, const paint *pt, float lift)
{
    int i;
    float along = 0, total = 0;
    for (i = 0; i + 1 < n; i++)
        total += sqrtf((p[i + 1][0] - p[i][0]) * (p[i + 1][0] - p[i][0]) + (p[i + 1][1] - p[i][1]) * (p[i + 1][1] - p[i][1]));
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
            tex_tile = WG_TX_ASPHALT;
            tex_quad = 1;
            quad(q[0], q[1], q[2], q[3], 0x565B66u, 0.92f);
            tex_tile = WG_TX_PLAIN;
            tex_quad = 0;
        }
        if (pt && (along < pt->quiet0 || total - along - l < pt->quiet1)) {
            along += l;
            continue;
        }
        if (pt && pt->edges) {                       /* edge lines: MEDIUM and HIGH */
            int save = variant, e;
            variant = WG_MEDIUM_UP;
            for (e = -1; e <= 1; e += 2) {
                float o0 = e * (1.0f - 0.45f / half), o1 = e * (1.0f - 0.27f / half);
                float m[4][3] = { { p[i][0] + nx0 * o0, p[i][2] + lift + 0.06f, -(p[i][1] + ny0 * o0) },
                                  { p[i + 1][0] + nx1 * o0, p[i + 1][2] + lift + 0.06f, -(p[i + 1][1] + ny1 * o0) },
                                  { p[i + 1][0] + nx1 * o1, p[i + 1][2] + lift + 0.06f, -(p[i + 1][1] + ny1 * o1) },
                                  { p[i][0] + nx0 * o1, p[i][2] + lift + 0.06f, -(p[i][1] + ny0 * o1) } };
                if (e > 0)
                    quad(m[0], m[1], m[2], m[3], 0xF2F2EAu, 1.0f);
                else
                    quad(m[3], m[2], m[1], m[0], 0xF2F2EAu, 1.0f);
            }
            variant = save;
        }
        if (pt && pt->dashes) {                      /* a bold dash over each piece's first half */
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
    for (i = -n; i <= n; i++) {                   /* each street, level at the town's height */
        float x0, y0, x1, y1;
        town_frame(t, i * GRID, -n * GRID, &x0, &y0);
        town_frame(t, i * GRID, n * GRID, &x1, &y1);
        level_segment(x0, y0, t->h, x1, y1, t->h, TOWN_FLAT, TOWN_EASE);
        town_frame(t, -n * GRID, i * GRID, &x0, &y0);
        town_frame(t, n * GRID, i * GRID, &x1, &y1);
        level_segment(x0, y0, t->h, x1, y1, t->h, TOWN_FLAT, TOWN_EASE);
    }
}

static float road_distance(float x, float y)
{
    float best = 1e30f;
    int i, e;
    for (i = 0; i < ntowns; i++) {                /* a roundabout: its ring, and all inside it */
        float dx = x - places[i].x, dy = y - places[i].y, d = sqrtf(dx * dx + dy * dy) - WG_RING_R;
        d = DGK_MAX(d, 0.0f);
        if (d * d < best)
            best = d * d;
    }
    /* To the roads' middle lines, segment by segment (their points are 8 m apart: a house between two could
     * have every corner 6 m from both with the road through its middle). */
    for (e = 0; e < nedges; e++)
        for (i = (int)edges[e].first_point; i + 1 < (int)(edges[e].first_point + edges[e].npoints); i++) {
            float sx = pts[i + 1][0] - pts[i][0], sy = pts[i + 1][1] - pts[i][1], l2 = sx * sx + sy * sy;
            float t = l2 > 1e-6f ? DGK_CLAMP(((x - pts[i][0]) * sx + (y - pts[i][1]) * sy) / l2, 0.0f, 1.0f) : 0.0f;
            float dx = pts[i][0] + sx * t - x, dy = pts[i][1] + sy * t - y, d = dx * dx + dy * dy;
            if (d < best)
                best = d;
        }
    return sqrtf(best);
}

/* A town's street from (x0, y0) to (x1, y1) at height h, in pieces 16 m
 * long: a triangle goes to the chunk holding its middle, so one long quad
 * was drawn only while that chunk was in view, and the fog (worked out at
 * the corners) washed the whole of it out from its far ends. */
static void street(float x0, float y0, float x1, float y1, float h, float lift)
{
    static float s[64][3];
    float l = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    int n = DGK_CLAMP((int)ceilf(l / 16.0f), 1, 63), k;
    for (k = 0; k <= n; k++) {
        s[k][0] = x0 + (x1 - x0) * k / n;
        s[k][1] = y0 + (y1 - y0) * k / n;
        s[k][2] = h;
    }
    road_ribbon((const float (*)[3])s, n + 1, 4.5f, NULL, lift);
}

static void lamp(float x, float y, float tall, float ax, float ay);

/* ---- a town's blocks: terraces, gardens, pavements, a park, a landmark ----------------
 *
 * Each block inside the town has a terrace along its two sides that face
 * the streets across `heading` (v), set back ROW_SET from the street's
 * middle (4.5 m of street, 2 of pavement, 2.5 of front garden), ROW_DEPTH
 * deep, ROW_END in from the cross streets; back gardens between them. LOW
 * draws a run of houses (those clear of the main roads, side by side) as
 * one block, which is also its collision box; MEDIUM and HIGH draw each
 * house on that footprint. The four blocks round the roundabout have shops;
 * one block nearby is a park, another the town's landmark. */
#define ROW_SET 9.0f
#define ROW_DEPTH 9.0f
#define ROW_END 12.0f

typedef struct hse {
    float a, b;                       /* along the row, in its frame */
    float eave, ridge;                /* above the ground; a shop's ridge is its flat top */
    uint32_t wall, roof, door, trim;
    int shop, chimney, dormer, door_left;
} hse;

static const uint32_t door_colours[] = { 0xC8302Au, 0x2E5FB8u, 0x2F8A4Au, 0xF2C230u, 0x2A2A30u, 0xF4F4F0u };
static const uint32_t awnings[] = { 0xE8402Au, 0x3A6FD8u, 0x2F9A5Au, 0xF28C28u, 0x8A4AB8u, 0xE84A8Au };
static const uint32_t signs[] = { 0x1C2E5Au, 0x7A1E1Eu, 0x1E5A3Au, 0x3A2A5Au };

static void hedge_stretch(float ax, float ay, float ah, float bx, float by, float bh, uint32_t col);
static void pyramid(float x, float y, float angle, float half, float z0, float z1, uint32_t rgb);

static bframe sub_frame(const bframe *f, float u, float v)
{
    bframe r = *f;
    r.cx = f->cx + f->c * u - f->s * v;
    r.cy = f->cy + f->s * u + f->c * v;
    return r;
}

/* GL directions of the frame's +u and +v. */
static void fdir(const bframe *f, int along_v, float sign, float *out)
{
    out[0] = (along_v ? -f->s : f->c) * sign;
    out[1] = 0;
    out[2] = -(along_v ? f->c : f->s) * sign;
}

/* A wall at v = `at` from u0 to u1 (or, across, at u = `at` from v0 to v1), from h0 to h1, facing `sign`. */
static void wall_at(cmesh *m, const bframe *f, int across, float at, float a0, float a1, float h0, float h1,
                    float sign, uint32_t rgb, float foot)
{
    float p[4][3], out[3];
    if (!across) {
        bpoint(f, a0, at, h0, p[0]);
        bpoint(f, a1, at, h0, p[1]);
        bpoint(f, a1, at, h1, p[2]);
        bpoint(f, a0, at, h1, p[3]);
    } else {
        bpoint(f, at, a0, h0, p[0]);
        bpoint(f, at, a1, h0, p[1]);
        bpoint(f, at, a1, h1, p[2]);
        bpoint(f, at, a0, h1, p[3]);
    }
    fdir(f, !across, sign, out);
    face(m, p, 4, out, rgb, foot);
}

/* A flat top at height h over u0..u1, v0..v1. */
static void top_at(cmesh *m, const bframe *f, float u0, float u1, float v0, float v1, float h, uint32_t rgb)
{
    float p[4][3], up[3] = { 0, 1, 0 };
    bpoint(f, u0, v0, h, p[0]);
    bpoint(f, u1, v0, h, p[1]);
    bpoint(f, u1, v1, h, p[2]);
    bpoint(f, u0, v1, h, p[3]);
    face(m, p, 4, up, rgb, -1e9f);
}

/* A box about (u, v), half sizes hu and hv, from h0 to h1, its top in `top`. */
static void fbox(cmesh *m, const bframe *f, float u, float v, float hu, float hv, float h0, float h1, uint32_t rgb,
                 uint32_t top)
{
    wall_at(m, f, 0, v - hv, u - hu, u + hu, h0, h1, -1, rgb, h0);
    wall_at(m, f, 0, v + hv, u - hu, u + hu, h0, h1, 1, rgb, h0);
    wall_at(m, f, 1, u - hu, v - hv, v + hv, h0, h1, -1, rgb, h0);
    wall_at(m, f, 1, u + hu, v - hv, v + hv, h0, h1, 1, rgb, h0);
    top_at(m, f, u - hu, u + hu, v - hv, v + hv, h1, top);
}

/* A roof slope from the eave (at v = ve, height he) up to the ridge (v = vr, height hr), u0..u1. */
static void slope_at(cmesh *m, const bframe *f, float u0, float u1, float ve, float he, float vr, float hr, uint32_t rgb)
{
    float p[4][3], out[3];
    bpoint(f, u0, ve, he, p[0]);
    bpoint(f, u1, ve, he, p[1]);
    bpoint(f, u1, vr, hr, p[2]);
    bpoint(f, u0, vr, hr, p[3]);
    fdir(f, 1, ve < vr ? -1.0f : 1.0f, out);
    out[0] *= 0.6f;
    out[2] *= 0.6f;
    out[1] = 0.8f;
    face(m, p, 4, out, rgb, -1e9f);
}

/* A gable triangle at u = `at` (facing `sign` along u), its base at h0 across the depth, its peak at h1. */
static void gable_at(cmesh *m, const bframe *f, float at, float hw, float h0, float h1, float sign, uint32_t rgb)
{
    float p[3][3], out[3];
    bpoint(f, at, -hw, h0, p[0]);
    bpoint(f, at, hw, h0, p[1]);
    bpoint(f, at, 0, h1, p[2]);
    fdir(f, 0, sign, out);
    face(m, p, 3, out, rgb, -1e9f);
}

/* One house of a terrace, in the row's frame f (its front at v = -hw, facing -v); base: the ground. The
 * neighbours (NULL: the run ends there) decide its end walls and gables. */
static void row_house(const bframe *f, const hse *h, const hse *left, const hse *right, float base, int run_l,
                      int run_r)
{
    static const uint32_t glass = 0x8FCBEFu;
    const float hw = ROW_DEPTH * 0.5f, o = 0.4f;
    float uc = (h->a + h->b) * 0.5f, w = h->b - h->a, e = base + h->eave, r = base + h->ridge, du, k;
    cmesh *m;
    bframe at = sub_frame(f, uc, 0);
    int i, nwin;
    m = chunk_for(at.cx, at.cy);
    /* Front and back walls; the ends where the run ends or a lower neighbour leaves them showing. */
    tex_tile = h->shop || (h->roof & 1) ? WG_TX_BOARDS : WG_TX_BRICK;   /* the look test */
    tex_quad = 1;
    wall_at(m, f, 0, -hw, h->a, h->b, base - 0.5f, e, -1, h->wall, base);
    wall_at(m, f, 0, hw, h->a, h->b, base - 0.5f, e, 1, h->wall, base);
    tex_tile = WG_TX_PLAIN;
    tex_quad = 0;
    for (i = 0; i < 2; i++) {
        const hse *nb = i ? right : left;
        float u = i ? h->b : h->a, sign = i ? 1.0f : -1.0f;
        float nbe = nb ? base + nb->eave : base - 0.5f, nbr = nb ? base + nb->ridge : base - 0.5f;
        if (nbe < e - 0.05f)
            wall_at(m, f, 1, u, -hw, hw, nbe, e, sign, h->wall, base);
        if (!h->shop && (nbr < r - 0.05f || !nb))
            gable_at(m, f, u, hw, e, r, sign, h->wall);
        if (h->shop && (!nb || nbe < e - 0.05f || nb->shop == 0))
            wall_at(m, f, 1, u, -hw, hw, e, e + 0.5f, sign, h->wall, base);
    }
    if (h->shop) {
        float lo = base + 0.3f, hi = base + 2.6f;
        top_at(m, f, h->a, h->b, -hw, hw, e + 0.5f, mb_shade(h->roof, 1.05f));
        wall_at(m, f, 0, -hw, h->a, h->b, e, e + 0.5f, -1, h->wall, base);
        wall_at(m, f, 0, hw, h->a, h->b, e, e + 0.5f, 1, h->wall, base);
        /* The shopfront: its window, a door in it; the awning over, the sign above. */
        wall_at(m, f, 0, -hw - 0.03f, h->a + 0.5f, h->b - 0.5f, lo, hi, -1, glass, -1e9f);
        du = h->door_left ? h->a + 1.4f : h->b - 1.4f;
        wall_at(m, f, 0, -hw - 0.05f, du - 0.55f, du + 0.55f, base, base + 2.2f, -1, h->door, -1e9f);
        {
            float p[4][3], out[3];
            bpoint(f, h->a + 0.3f, -hw, base + 3.15f, p[0]);
            bpoint(f, h->b - 0.3f, -hw, base + 3.15f, p[1]);
            bpoint(f, h->b - 0.3f, -hw - 1.6f, base + 2.65f, p[2]);
            bpoint(f, h->a + 0.3f, -hw - 1.6f, base + 2.65f, p[3]);
            fdir(f, 1, -1, out);
            out[1] = 1.2f;
            face(m, p, 4, out, h->trim, -1e9f);
        }
        wall_at(m, f, 0, -hw - 0.04f, h->a + 0.4f, h->b - 0.4f, base + 3.3f, base + 4.0f, -1, h->door == 0xF4F4F0u ? 0x1C2E5Au : signs[(int)(uc * 7) & 3], -1e9f);
        nwin = w > 8.5f ? 3 : 2;
        for (k = base + 4.6f; k + 1.4f < e - 0.2f; k += 3.0f)       /* the flats above: rows of windows */
            for (i = 0; i < nwin; i++) {
                float a = h->a + (i + 0.5f) * (w / nwin);
                wall_at(m, f, 0, -hw - 0.03f, a - 0.6f, a + 0.6f, k, k + 1.4f, -1, glass, -1e9f);
                wall_at(m, f, 0, hw + 0.03f, a - 0.6f, a + 0.6f, k, k + 1.4f, 1, glass, -1e9f);
            }
        return;
    }
    /* The roof's two slopes, overhanging the front and back; past the run's ends a little. */
    {
        float u0 = h->a - (run_l ? 0.3f : 0.0f), u1 = h->b + (run_r ? 0.3f : 0.0f), eh = e - o * (r - e) / hw;
        tex_tile = WG_TX_ROOF;
        tex_quad = 1;
        slope_at(m, f, u0, u1, -hw - o, eh, 0, r, h->roof);
        slope_at(m, f, u0, u1, hw + o, eh, 0, r, h->roof);
        tex_tile = WG_TX_PLAIN;
        tex_quad = 0;
    }
    if (h->chimney) {                                /* at one end, on the ridge: a third of them at MEDIUM */
        float cu = h->door_left ? h->b - 0.7f : h->a + 0.7f;
        int save = variant;
        variant = h->chimney == 1 ? WG_MEDIUM_UP : WG_HIGH_ONLY;
        m = chunk_for(at.cx, at.cy);
        fbox(m, f, cu, 0.9f, 0.35f, 0.3f, r - 1.4f, r + 0.9f, 0xB0573Au, 0x5A3A2Au);
        variant = save;
        m = chunk_for(at.cx, at.cy);
    }
    /* The front: a door, a window by it, the windows above; the back's windows above. */
    du = h->door_left ? h->a + 1.3f : h->b - 1.3f;
    wall_at(m, f, 0, -hw - 0.03f, du - 0.5f, du + 0.5f, base, base + 2.1f, -1, h->door, -1e9f);
    {
        float wu = h->door_left ? (du + 0.5f + h->b) * 0.5f : (h->a + du - 0.5f) * 0.5f;
        wall_at(m, f, 0, -hw - 0.03f, wu - 0.75f, wu + 0.75f, base + 0.9f, base + 2.2f, -1, glass, -1e9f);
    }
    nwin = w > 7.5f ? 2 : 1;
    for (i = 0; i < nwin; i++) {
        float a = h->a + (i + 0.5f) * (w / nwin);
        wall_at(m, f, 0, -hw - 0.03f, a - 0.6f, a + 0.6f, e - 2.0f, e - 0.7f, -1, glass, -1e9f);
    }
}

/* HIGH's touches on a house: white frames round its windows, a hood over the door, a box of flowers under
 * the front window, and (some) a dormer in the front slope. */
static void row_house_high(const bframe *f, const hse *h, float base)
{
    static const uint32_t flowers[] = { 0xE8402Au, 0xF2C230u, 0xE84A8Au, 0x8A4AB8u };
    const float hw = ROW_DEPTH * 0.5f;
    float uc = (h->a + h->b) * 0.5f, w = h->b - h->a, e = base + h->eave, r = base + h->ridge, du, wu;
    bframe at = sub_frame(f, uc, 0);
    cmesh *m = chunk_for(at.cx, at.cy);
    int i, nwin = w > 7.5f ? 2 : 1;
    if (h->shop) {
        /* The awning's white stripes. */
        float p[4][3], out[3], a;
        for (a = h->a + 0.3f + 0.45f; a + 0.45f < h->b - 0.3f; a += 1.8f) {
            bpoint(f, a, -hw - 0.01f, base + 3.17f, p[0]);
            bpoint(f, a + 0.9f, -hw - 0.01f, base + 3.17f, p[1]);
            bpoint(f, a + 0.9f, -hw - 1.61f, base + 2.67f, p[2]);
            bpoint(f, a, -hw - 1.61f, base + 2.67f, p[3]);
            fdir(f, 1, -1, out);
            out[1] = 1.2f;
            face(m, p, 4, out, 0xF6F4EEu, -1e9f);
        }
        return;
    }
    for (i = 0; i < nwin; i++) {                     /* frames round the front's windows; the back's windows */
        float a = h->a + (i + 0.5f) * (w / nwin);
        wall_at(m, f, 0, -hw - 0.02f, a - 0.75f, a + 0.75f, e - 2.15f, e - 0.55f, -1, h->trim, -1e9f);
        wall_at(m, f, 0, hw + 0.03f, a - 0.6f, a + 0.6f, e - 2.0f, e - 0.7f, 1, 0x8FCBEFu, -1e9f);
    }
    du = h->door_left ? h->a + 1.3f : h->b - 1.3f;
    wu = h->door_left ? (du + 0.5f + h->b) * 0.5f : (h->a + du - 0.5f) * 0.5f;
    wall_at(m, f, 0, -hw - 0.02f, wu - 0.9f, wu + 0.9f, base + 0.75f, base + 2.35f, -1, h->trim, -1e9f);
    fbox(m, f, wu, -hw - 0.3f, 0.85f, 0.25f, base + 0.5f, base + 0.85f, 0x8A5A3Au, flowers[(int)(uc * 3.7f) & 3]);
    {   /* the door's hood */
        float p[4][3], out[3];
        bpoint(f, du - 0.75f, -hw, base + 2.45f, p[0]);
        bpoint(f, du + 0.75f, -hw, base + 2.45f, p[1]);
        bpoint(f, du + 0.75f, -hw - 0.8f, base + 2.2f, p[2]);
        bpoint(f, du - 0.75f, -hw - 0.8f, base + 2.2f, p[3]);
        fdir(f, 1, -1, out);
        out[1] = 1.0f;
        face(m, p, 4, out, h->roof, -1e9f);
    }
    if (h->dormer && w > 6.5f) {                     /* a dormer: a little gabled window in the front slope */
        float dv = -hw * 0.45f, slope_h = e + (r - e) * (1.0f - 0.45f), dh0 = slope_h - 0.2f, dh1 = slope_h + 1.3f;
        bframe d = sub_frame(f, uc, 0);
        wall_at(m, &d, 0, dv - 0.6f, -0.8f, 0.8f, dh0, dh1, -1, h->wall, -1e9f);
        wall_at(m, &d, 0, dv - 0.62f, -0.5f, 0.5f, dh0 + 0.3f, dh1 - 0.2f, -1, 0x8FCBEFu, -1e9f);
        {
            /* Its gable over the window, and its little roof's two slopes running back into the main roof. */
            float p[4][3], out[3];
            int sd;
            bpoint(&d, -0.8f, dv - 0.6f, dh1, p[0]);
            bpoint(&d, 0.8f, dv - 0.6f, dh1, p[1]);
            bpoint(&d, 0.0f, dv - 0.6f, dh1 + 0.7f, p[2]);
            fdir(&d, 1, -1, out);
            face(m, p, 3, out, h->wall, -1e9f);
            for (sd = -1; sd <= 1; sd += 2) {
                bpoint(&d, sd * 0.95f, dv - 0.75f, dh1 - 0.05f, p[0]);
                bpoint(&d, sd * 0.95f, dv + 1.8f, dh1 - 0.05f, p[1]);
                bpoint(&d, 0.0f, dv + 1.8f, dh1 + 0.72f, p[2]);
                bpoint(&d, 0.0f, dv - 0.75f, dh1 + 0.72f, p[3]);
                fdir(&d, 0, (float)sd, out);
                out[0] *= 0.6f;
                out[2] *= 0.6f;
                out[1] = 0.8f;
                face(m, p, 4, out, h->roof, -1e9f);
            }
        }
    }
}

static void tree_round(float x, float y, float s, uint32_t col, int collide);

/* The row's houses, from a to b along it (frame f: its middle, front facing -v), those whose footprint is
 * clear of the main roads; shops or cottages. LOW: a block a run (its collision); MEDIUM and HIGH: the
 * houses, their front gardens (a low hedge, a path to the door) and, behind, a tree now and then. */
static void terrace_row(const place *t, const bframe *f, float a, float b, int shops)
{
    static const uint32_t trims[] = { 0xF4F2EAu, 0xF4F2EAu, 0xE8E0C8u, 0x2A2A30u };
    hse hs[16];
    int n = 0, i, ok[16];
    float u = a, base = t->h;
    while (u < b - 4.0f && n < 16) {
        hse *h = &hs[n];
        float w = shops ? 8.0f + frand() * 4.0f : 6.0f + frand() * 4.0f;
        if (b - (u + w) < (shops ? 7.0f : 5.5f))
            w = b - u;                               /* the last takes what is left */
        h->a = u;
        h->b = u + w;
        h->shop = shops;
        h->eave = shops ? 7.0f + frand() * 2.5f : 4.8f + frand() * 1.4f;
        h->ridge = shops ? h->eave : h->eave + 2.2f + frand() * 0.8f;
        h->wall = house_walls[dgk_rng_below(&rng, 6)];
        h->roof = house_roofs[dgk_rng_below(&rng, 6)];
        h->door = door_colours[dgk_rng_below(&rng, 6)];
        h->trim = shops ? awnings[dgk_rng_below(&rng, 6)] : trims[dgk_rng_below(&rng, 4)];
        h->chimney = (int)dgk_rng_below(&rng, 3);      /* 1: at MEDIUM and HIGH; 2: at HIGH; 0: none */
        h->dormer = dgk_rng_below(&rng, 3) == 0;
        h->door_left = (int)dgk_rng_below(&rng, 2);
        {
            /* Clear of the main roads (and the roundabout): every corner of it and its front garden 6 m off. */
            int c;
            ok[n] = 1;
            for (c = 0; c < 4 && ok[n]; c++) {
                bframe at = sub_frame(f, c & 1 ? h->b : h->a, c & 2 ? ROW_DEPTH * 0.5f : -ROW_DEPTH * 0.5f - 2.5f);
                ok[n] = road_distance(at.cx, at.cy) >= 6.0f;
            }
        }
        u += w;
        n++;
    }
    for (i = 0; i < n; i++) {
        int j, k;
        float sum = 0, uc;
        bframe c;
        if (!ok[i] || (i > 0 && ok[i - 1]))
            continue;
        for (j = i; j < n && ok[j]; j++)             /* a run: i .. j - 1 */
            sum += hs[j].eave + (hs[j].shop ? 0.5f : 1.3f);
        uc = (hs[i].a + hs[j - 1].b) * 0.5f;
        c = sub_frame(f, uc, 0);
        variant = WG_LOW_ONLY;
        block(c.cx, c.cy, (hs[j - 1].b - hs[i].a) * 0.5f, ROW_DEPTH * 0.5f, atan2f(f->s, f->c), sum / (j - i),
              hs[i].wall, hs[i].roof, 1, 1);
        variant = WG_MEDIUM_UP;
        bshadow(&c, (hs[j - 1].b - hs[i].a) * 0.5f, ROW_DEPTH * 0.5f, sum / (j - i));
        for (k = i; k < j; k++) {
            const hse *h = &hs[k];
            float du = h->door_left ? h->a + 1.3f : h->b - 1.3f, fv = -ROW_DEPTH * 0.5f;
            bframe p0, p1;
            variant = WG_MEDIUM_UP;
            row_house(f, h, k > i ? &hs[k - 1] : NULL, k + 1 < j ? &hs[k + 1] : NULL, base, k == i, k == j - 1);
            /* The front garden: a low hedge (or a white wall) along the pavement, a gap and a path at the door. */
            if (!h->shop) {
                uint32_t hedge = (k & 3) == 3 ? 0xEDEAE0u : 0x3E8E36u;
                p0 = sub_frame(f, h->a + 0.2f, fv - 2.3f);
                p1 = sub_frame(f, du - 0.7f, fv - 2.3f);
                hedge_stretch(p0.cx, p0.cy, base - 0.3f, p1.cx, p1.cy, base - 0.3f, hedge);
                p0 = sub_frame(f, du + 0.7f, fv - 2.3f);
                p1 = sub_frame(f, h->b - 0.2f, fv - 2.3f);
                hedge_stretch(p0.cx, p0.cy, base - 0.3f, p1.cx, p1.cy, base - 0.3f, hedge);
                {
                    bframe q0 = sub_frame(f, du - 0.55f, fv), q1 = sub_frame(f, du + 0.55f, fv);
                    bframe q2 = sub_frame(f, du + 0.55f, fv - 2.6f), q3 = sub_frame(f, du - 0.55f, fv - 2.6f);
                    ground_quad(q3.cx, q3.cy, q2.cx, q2.cy, q1.cx, q1.cy, q0.cx, q0.cy, 0.16f, 0xD8CFC0u, 0.95f);
                }
                if (frand() < 0.4f) {                /* a tree in the back garden: a third of them at MEDIUM */
                    bframe g = sub_frame(f, (h->a + h->b) * 0.5f + (frand() - 0.5f) * 2.0f,
                                         ROW_DEPTH * 0.5f + 5.0f + frand() * 5.0f);
                    tree_round(g.cx, g.cy, 1.2f + frand() * 0.6f, 0, frand() < 0.33f ? 0 : -1);
                }
            }
            variant = WG_HIGH_ONLY;
            row_house_high(f, h, base);
            variant = WG_SHARED;
        }
    }
}

/* Pavements (MEDIUM and HIGH) along a block's four sides: 2 m of stone, raised, a kerb on the street's side;
 * in 16 m pieces, none across a main road or the roundabout. */
static void pavements(const place *t, float u0, float u1, float v0, float v1)
{
    const float in = 4.5f, out = 6.5f, lift = 0.3f;
    int side;
    variant = WG_MEDIUM_UP;
    for (side = 0; side < 4; side++) {
        /* Along v-streets (sides 0, 1: at v0 and v1) from u0+in to u1-in; along u-streets from v0+out to v1-out. */
        int across = side >= 2;
        float at = side == 0 ? v0 : side == 1 ? v1 : side == 2 ? u0 : u1, dir = (side & 1) ? -1.0f : 1.0f;
        float a0 = across ? v0 + out : u0 + in, a1 = across ? v1 - out : u1 - in, a;
        for (a = a0; a < a1 - 0.5f; a += 16.0f) {
            float b = DGK_MIN(a + 16.0f, a1), q[4][2], p[4][3], pk[4][3], up[3] = { 0, 1, 0 }, kout[3];
            float mu = across ? at + dir * 5.5f : (a + b) * 0.5f, mv = across ? (a + b) * 0.5f : at + dir * 5.5f;
            int k;
            town_frame(t, mu, mv, &q[0][0], &q[0][1]);
            if (road_distance(q[0][0], q[0][1]) < 6.5f)
                continue;
            for (k = 0; k < 4; k++) {
                float al = k == 0 || k == 3 ? a : b, off = k < 2 ? in : out;
                if (across)
                    town_frame(t, at + dir * off, al, &q[k][0], &q[k][1]);
                else
                    town_frame(t, al, at + dir * off, &q[k][0], &q[k][1]);
                p[k][0] = q[k][0];
                p[k][1] = t->h + lift;
                p[k][2] = -q[k][1];
            }
            tex_tile = WG_TX_PAVING;
            tex_quad = 1;
            face(chunk_for((q[0][0] + q[2][0]) * 0.5f, (q[0][1] + q[2][1]) * 0.5f), p, 4, up, 0xCFC9BCu, -1e9f);
            tex_tile = WG_TX_PLAIN;
            tex_quad = 0;
            for (k = 0; k < 2; k++) {                /* the kerb: the street side's face */
                pk[k][0] = p[k][0];
                pk[k][2] = p[k][2];
                pk[k][1] = t->h + lift;
                pk[3 - k][0] = p[k][0];
                pk[3 - k][2] = p[k][2];
                pk[3 - k][1] = t->h + 0.1f;
            }
            {
                float wx, wy;
                if (across) {
                    town_frame(t, -dir, 0, &wx, &wy);
                    town_frame(t, 0, 0, &kout[0], &kout[2]);
                } else {
                    town_frame(t, 0, -dir, &wx, &wy);
                    town_frame(t, 0, 0, &kout[0], &kout[2]);
                }
                kout[0] = wx - kout[0];
                kout[1] = 0;
                kout[2] = -(wy - kout[2]);
            }
            variant = WG_HIGH_ONLY;
            face(chunk_for(p[0][0], -p[0][2]), pk, 4, kout, 0xEDE9DFu, -1e9f);
            variant = WG_MEDIUM_UP;
        }
    }
    variant = WG_SHARED;
}

/* The back gardens' hedges (MEDIUM and HIGH): down the block's middle and across its ends. */
static void garden_hedges(const place *t, float u0, float u1, float v0, float v1)
{
    float vm = (v0 + v1) * 0.5f, ua = u0 + ROW_SET + ROW_DEPTH + 0.5f, ub = u1 - ROW_SET - ROW_DEPTH - 0.5f,
          va = v0 + ROW_SET + ROW_DEPTH + 0.5f,
          vb = v1 - ROW_SET - ROW_DEPTH - 0.5f, x0, y0, x1, y1, a;
    variant = WG_MEDIUM_UP;
    for (a = ua; a < ub - 1.0f; a += 16.0f) {
        town_frame(t, a, vm, &x0, &y0);
        town_frame(t, DGK_MIN(a + 16.0f, ub), vm, &x1, &y1);
        if (road_distance((x0 + x1) * 0.5f, (y0 + y1) * 0.5f) > 6.0f)
            hedge_stretch(x0, y0, t->h - 0.1f, x1, y1, t->h - 0.1f, 0x3E8E36u);
    }
    (void)va;
    (void)vb;
    variant = WG_SHARED;
}

/* A flat disc lying at height h, radius r, n sides, in the chunk at its middle. */
static void disc_at(float x, float y, float h, float r, int n, uint32_t rgb)
{
    float up[3] = { 0, 1, 0 }, p[3][3];
    cmesh *m = chunk_for(x, y);
    int k;
    for (k = 0; k < n; k++) {
        float a0 = 6.2831853f * k / n, a1 = 6.2831853f * (k + 1) / n;
        p[0][0] = x; p[0][1] = h; p[0][2] = -y;
        p[1][0] = x + r * cosf(a0); p[1][1] = h; p[1][2] = -(y + r * sinf(a0));
        p[2][0] = x + r * cosf(a1); p[2][1] = h; p[2][2] = -(y + r * sinf(a1));
        face(m, p, 3, up, rgb, -1e9f);
    }
}

/* A round tree at (x, y), `s` its size: solid (collide 1: a spire at LOW too, and a collision box), or only
 * MEDIUM and HIGH's (0) or HIGH's (-1): a garden's, behind hedges and houses. col 0: a green of the woods'. */
static void tree_round(float x, float y, float s, uint32_t col, int collide)
{
    static const uint32_t leaves[] = { 0x3E9E3Au, 0x4CB040u, 0x2F8A38u, 0x6AC24Au };
    float g = ground(x, y);
    int save = variant;
    if (!col)
        col = leaves[dgk_rng_below(&rng, 4)];
    if (collide > 0) {                               /* LOW: a four-sided spire, 4 triangles */
        variant = WG_LOW_ONLY;
        pyramid(x, y, 0, s * 1.1f, g + 0.4f, g + 2.4f + s * 2.6f, col);
        add_box(x, y, 0.5f, 0.5f, 0);
    }
    variant = collide < 0 ? WG_HIGH_ONLY : WG_MEDIUM_UP;
    trunk(x, y, g - 0.3f, g + 2.2f + s * 0.4f, 0.32f, 5, 0x7A5230u);
    blob(x, y, g + 1.8f + s * 1.25f, s * 1.25f, s * 1.15f, col);
    variant = save;
}

/* A park on a block: paths corner to corner, a fountain where they cross (solid; at LOW a low block of
 * water), flower beds, solid trees round its edge; HIGH: benches. */
static void park(const place *t, float u0, float u1, float v0, float v1)
{
    static const uint32_t beds[] = { 0xE8402Au, 0xF2C230u, 0xE84A8Au, 0xF4F4F0u };
    float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, x, y, h = t->h;
    int k;
    variant = WG_MEDIUM_UP;
    for (k = 0; k < 2; k++) {                        /* the paths */
        float au = u0 + 9.0f, bu = u1 - 9.0f, av = k ? v1 - 9.0f : v0 + 9.0f, bv = k ? v0 + 9.0f : v1 - 9.0f;
        float du = bu - au, dv = bv - av, l = sqrtf(du * du + dv * dv), nu = -dv / l * 1.3f, nv = du / l * 1.3f;
        float q[4][2];
        town_frame(t, au + nu, av + nv, &q[0][0], &q[0][1]);
        town_frame(t, bu + nu, bv + nv, &q[1][0], &q[1][1]);
        town_frame(t, bu - nu, bv - nv, &q[2][0], &q[2][1]);
        town_frame(t, au - nu, av - nv, &q[3][0], &q[3][1]);
        ground_quad(q[3][0], q[3][1], q[2][0], q[2][1], q[1][0], q[1][1], q[0][0], q[0][1], 0.14f + 0.01f * k,
                    0xE6DCC0u, 0.95f);
        ground_quad(q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], q[3][0], q[3][1], 0.14f + 0.01f * k,
                    0xE6DCC0u, 0.95f);
    }
    town_frame(t, cu, cv, &x, &y);
    /* The fountain: a stone basin of water, a column with a bowl, the water rising out of it. */
    variant = WG_LOW_ONLY;
    block(x, y, 3.0f, 3.0f, t->heading, 0.7f, 0xD8D0C0u, 0x5FB8E8u, 0, 0);
    for (k = 0; k < 4; k++)
        add_box(x, y, 3.3f * cosf(WG_PI / 8), 3.3f * cosf(WG_PI / 8) * tanf(WG_PI / 8), t->heading + k * WG_PI / 4);
    variant = WG_MEDIUM_UP;
    trunk(x, y, h - 0.2f, h + 0.75f, 3.3f, 12, 0xD8D0C0u);
    disc_at(x, y, h + 0.6f, 3.2f, 12, 0x5FB8E8u);
    trunk(x, y, h + 0.6f, h + 2.4f, 0.35f, 6, 0xD8D0C0u);
    blob(x, y, h + 2.45f, 1.1f, 0.3f, 0xD8D0C0u);
    blob(x, y, h + 3.0f, 0.45f, 0.8f, 0xBFE6FAu);
    /* Flower beds beside the fountain: a round bed of earth, flowers on it (HIGH: more). */
    for (k = 0; k < 2; k++) {
        float bx, by;
        int f;
        town_frame(t, cu + (k ? 13.0f : -13.0f), cv, &bx, &by);
        disc_at(bx, by, h + 0.18f, 2.6f, 10, 0x6A4A30u);
        for (f = 0; f < 5; f++) {
            float a = 6.2831853f * f / 5 + k;
            variant = f < 3 ? WG_MEDIUM_UP : WG_HIGH_ONLY;
            blob(bx + 1.5f * cosf(a), by + 1.5f * sinf(a), h + 0.5f, 0.7f, 0.45f, beds[(f + k) & 3]);
        }
        variant = WG_MEDIUM_UP;
        blob(bx, by, h + 0.6f, 0.8f, 0.6f, beds[(k + 2) & 3]);
    }
    /* Trees round the edge, solid. */
    for (k = 0; k < 8; k++) {
        float tu = k < 3 ? u0 + 9.0f + k * 23.0f : k < 6 ? u0 + 9.0f + (k - 3) * 23.0f : k == 6 ? u0 + 9.0f : u1 - 9.0f;
        float tv = k < 3 ? v0 + 8.0f : k < 6 ? v1 - 8.0f : cv;
        float tx, ty;
        town_frame(t, tu, tv, &tx, &ty);
        if (road_distance(tx, ty) > 10.0f)
            tree_round(tx, ty, 1.5f + frand() * 0.8f, 0, 1);
    }
    /* HIGH: benches by the fountain, along the paths. */
    variant = WG_HIGH_ONLY;
    for (k = 0; k < 4; k++) {
        bframe b;
        float a = t->heading + WG_PI / 4 + k * WG_PI / 2;
        b.c = cosf(a + WG_PI / 2);
        b.s = sinf(a + WG_PI / 2);
        b.cx = x + 7.0f * cosf(a) + 2.3f * cosf(a + WG_PI / 2);
        b.cy = y + 7.0f * sinf(a) + 2.3f * sinf(a + WG_PI / 2);
        fbox(chunk_for(b.cx, b.cy), &b, 0, 0, 0.9f, 0.25f, h + 0.4f, h + 0.5f, 0x8A5A3Au, 0xA06A40u);
        fbox(chunk_for(b.cx, b.cy), &b, 0, 0.25f, 0.9f, 0.05f, h + 0.5f, h + 0.95f, 0x8A5A3Au, 0xA06A40u);
        fbox(chunk_for(b.cx, b.cy), &b, -0.75f, 0, 0.06f, 0.2f, h, h + 0.4f, 0x3A3A44u, 0x3A3A44u);
        fbox(chunk_for(b.cx, b.cy), &b, 0.75f, 0, 0.06f, 0.2f, h, h + 0.4f, 0x3A3A44u, 0x3A3A44u);
    }
    variant = WG_SHARED;
}

static void landmark(const place *t, float u0, float u1, float v0, float v1, int kind);

/* A low hedge round a block inside its pavements (MEDIUM and HIGH), a gap where a path comes in from the
 * street at v0 (gap_u, 0: none); in stretches, none across a main road. */
static void block_hedge(const place *t, float u0, float u1, float v0, float v1, float gap_u)
{
    const float in = 7.2f;
    float pts[5][2] = { { u0 + in, v0 + in }, { u1 - in, v0 + in }, { u1 - in, v1 - in }, { u0 + in, v1 - in },
                        { u0 + in, v0 + in } };
    int k;
    variant = WG_MEDIUM_UP;
    for (k = 0; k < 4; k++) {
        float a, len = sqrtf((pts[k + 1][0] - pts[k][0]) * (pts[k + 1][0] - pts[k][0]) +
                             (pts[k + 1][1] - pts[k][1]) * (pts[k + 1][1] - pts[k][1]));
        for (a = 0; a < len - 0.5f; a += 12.0f) {
            float b = DGK_MIN(a + 12.0f, len), ua, va, ub, vb, x0, y0, x1, y1;
            ua = pts[k][0] + (pts[k + 1][0] - pts[k][0]) * a / len;
            va = pts[k][1] + (pts[k + 1][1] - pts[k][1]) * a / len;
            ub = pts[k][0] + (pts[k + 1][0] - pts[k][0]) * b / len;
            vb = pts[k][1] + (pts[k + 1][1] - pts[k][1]) * b / len;
            if (k == 0 && gap_u != 0 && ua < gap_u + 1.6f && ub > gap_u - 1.6f) {   /* the gate */
                if (ua < gap_u - 1.6f) {
                    town_frame(t, ua, va, &x0, &y0);
                    town_frame(t, gap_u - 1.6f, va, &x1, &y1);
                    hedge_stretch(x0, y0, t->h - 0.35f, x1, y1, t->h - 0.35f, 0x3E8E36u);
                }
                if (ub > gap_u + 1.6f) {
                    town_frame(t, gap_u + 1.6f, va, &x0, &y0);
                    town_frame(t, ub, vb, &x1, &y1);
                    hedge_stretch(x0, y0, t->h - 0.35f, x1, y1, t->h - 0.35f, 0x3E8E36u);
                }
                continue;
            }
            town_frame(t, ua, va, &x0, &y0);
            town_frame(t, ub, vb, &x1, &y1);
            if (road_distance((x0 + x1) * 0.5f, (y0 + y1) * 0.5f) > 7.0f)
                hedge_stretch(x0, y0, t->h - 0.35f, x1, y1, t->h - 0.35f, 0x3E8E36u);
        }
    }
    variant = WG_SHARED;
}

/* A cone's frustum round (x, y): radius r0 at z0 to r1 at z1, n sides, smooth, no ends. */
static void frustum(float x, float y, float z0, float z1, float r0, float r1, int n, uint32_t rgb)
{
    cmesh *c = chunk_for(x, y);
    float slope = (r0 - r1) / (z1 - z0), l = sqrtf(1 + slope * slope);
    int lo[16], hi[16], i;
    for (i = 0; i < n; i++) {
        float a = 6.2831853f * i / n, p[3], nm[3];
        nm[0] = cosf(a) / l; nm[1] = slope / l; nm[2] = sinf(a) / l;
        p[0] = x + r0 * cosf(a); p[1] = z0; p[2] = -y + r0 * sinf(a);
        lo[i] = smooth_vertex(c, p, nm, rgb, 1.0f);
        p[0] = x + r1 * cosf(a); p[1] = z1; p[2] = -y + r1 * sinf(a);
        hi[i] = smooth_vertex(c, p, nm, rgb, 1.0f);
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        cmesh_tri(c, lo[j], lo[i], hi[i]);
        cmesh_tri(c, lo[j], hi[i], hi[j]);
    }
}

/* A four-sided pyramid on a square of half side `half` (turned `angle`) at z0, its tip at z1. */
static void pyramid(float x, float y, float angle, float half, float z0, float z1, uint32_t rgb)
{
    cmesh *m = chunk_for(x, y);
    int k;
    for (k = 0; k < 4; k++) {
        float a0 = angle + WG_PI / 4 + k * WG_PI / 2, a1 = a0 + WG_PI / 2, r = half * 1.41421356f;
        float p[3][3], out[3], am = (a0 + a1) * 0.5f;
        p[0][0] = x + r * cosf(a0); p[0][1] = z0; p[0][2] = -(y + r * sinf(a0));
        p[1][0] = x + r * cosf(a1); p[1][1] = z0; p[1][2] = -(y + r * sinf(a1));
        p[2][0] = x; p[2][1] = z1; p[2][2] = -y;
        out[0] = cosf(am); out[1] = 0.5f; out[2] = -sinf(am);
        face(m, p, 3, out, rgb, -1e9f);
    }
}

/* A clock face on the wall at v = `at` (facing -v) of frame f, centred at (u, h): a white disc, its hands. */
static void clock_face(const bframe *f, float u, float at, float h, float r)
{
    bframe c = sub_frame(f, u, at);
    cmesh *m = chunk_for(c.cx, c.cy);
    float out[3], p[4][3];
    int k;
    fdir(f, 1, -1, out);
    for (k = 0; k < 12; k++) {
        float a0 = 6.2831853f * k / 12, a1 = 6.2831853f * (k + 1) / 12, tri[3][3];
        bpoint(&c, 0, 0, h, tri[0]);
        bpoint(&c, r * cosf(a0), 0, h + r * sinf(a0), tri[1]);
        bpoint(&c, r * cosf(a1), 0, h + r * sinf(a1), tri[2]);
        face(m, tri, 3, out, 0xF6F4EEu, -1e9f);
    }
    bpoint(&c, -0.08f, -0.02f, h, p[0]);              /* the hour hand (ten to two, as a toy's) */
    bpoint(&c, 0.08f, -0.02f, h, p[1]);
    bpoint(&c, 0.5f * r + 0.08f, -0.02f, h + 0.45f * r, p[2]);
    bpoint(&c, 0.5f * r - 0.08f, -0.02f, h + 0.45f * r, p[3]);
    face(m, p, 4, out, 0x1C1C24u, -1e9f);
    bpoint(&c, -0.06f, -0.03f, h, p[0]);              /* the minute hand */
    bpoint(&c, 0.06f, -0.03f, h, p[1]);
    bpoint(&c, -0.55f * r + 0.06f, -0.03f, h + 0.7f * r, p[2]);
    bpoint(&c, -0.55f * r - 0.06f, -0.03f, h + 0.7f * r, p[3]);
    face(m, p, 4, out, 0x1C1C24u, -1e9f);
}

/* The town's landmark on a block, facing the street at v0: by `kind`, a church with a spire, a water tower,
 * a town hall with a clock tower, a windmill. Solid (LOW: boxes and pyramids, the same footprints). On a
 * lawn, a path to it from the street, trees beside. */
static void landmark(const place *t, float u0, float u1, float v0, float v1, int kind)
{
    float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, h = t->h, x, y, q[4][2];
    bframe f, r;
    cmesh *m;
    int k;
    f.c = cosf(t->heading);
    f.s = sinf(t->heading);
    town_frame(t, cu, cv, &f.cx, &f.cy);
    r = f;                                           /* turned a quarter: its u along the block's v */
    r.c = cosf(t->heading + WG_PI / 2);
    r.s = sinf(t->heading + WG_PI / 2);
    m = chunk_for(f.cx, f.cy);
    variant = WG_MEDIUM_UP;
    town_frame(t, cu - 1.4f, v0 + 6.5f, &q[0][0], &q[0][1]);    /* the path from the pavement */
    town_frame(t, cu + 1.4f, v0 + 6.5f, &q[1][0], &q[1][1]);
    town_frame(t, cu + 1.4f, cv - 8.0f, &q[2][0], &q[2][1]);
    town_frame(t, cu - 1.4f, cv - 8.0f, &q[3][0], &q[3][1]);
    ground_quad(q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], q[3][0], q[3][1], 0.15f, 0xE6DCC0u, 0.95f);
    variant = WG_HIGH_ONLY;                          /* flowers along the path */
    for (k = 0; k < 6; k++) {
        static const uint32_t fl[] = { 0xE8402Au, 0xF2C230u, 0xE84A8Au, 0xF4F4F0u };
        float fx, fy;
        town_frame(t, cu + (k & 1 ? 2.6f : -2.6f), v0 + 10.0f + (k >> 1) * 4.0f, &fx, &fy);
        blob(fx, fy, h + 0.4f, 0.9f, 0.4f, fl[k & 3]);
    }
    variant = WG_MEDIUM_UP;
    for (k = 0; k < 4; k++) {                        /* trees at its corners, solid */
        float tx, ty;
        town_frame(t, k & 1 ? u1 - 9.0f : u0 + 9.0f, k & 2 ? v1 - 9.0f : v0 + 12.0f, &tx, &ty);
        if (road_distance(tx, ty) > 10.0f)
            tree_round(tx, ty, 1.6f + frand() * 0.6f, 0, 1);
    }
    switch (kind % 4) {
    case 0: {
        /* A church: a nave along the block, a square tower at its front, a spire. */
        const uint32_t stone = 0xE8DFC8u, slate = 0x5A6478u, copper = 0x5FA88Au, glass = 0x3A5A9Au;
        bframe nave = sub_frame(&r, 4.0f, 0), tower = sub_frame(&f, 0, -10.0f);
        float x0, y0;
        variant = WG_LOW_ONLY;
        block(nave.cx, nave.cy, 11.0f, 6.0f, t->heading + WG_PI / 2, 10.0f, stone, slate, 1, 1);
        block(tower.cx, tower.cy, 3.0f, 3.0f, t->heading, 18.0f, stone, stone, 0, 1);
        pyramid(tower.cx, tower.cy, t->heading, 3.0f, h + 18.0f, h + 30.0f, copper);
        variant = WG_MEDIUM_UP;
        bshadow(&nave, 11.0f, 6.0f, 10.0f);
        wall_at(chunk_for(nave.cx, nave.cy), &nave, 0, -6.0f, -11.0f, 11.0f, h - 0.5f, h + 7.0f, -1, stone, h);
        wall_at(chunk_for(nave.cx, nave.cy), &nave, 0, 6.0f, -11.0f, 11.0f, h - 0.5f, h + 7.0f, 1, stone, h);
        wall_at(chunk_for(nave.cx, nave.cy), &nave, 1, 11.0f, -6.0f, 6.0f, h - 0.5f, h + 7.0f, 1, stone, h);
        gable_at(chunk_for(nave.cx, nave.cy), &nave, 11.0f, 6.0f, h + 7.0f, h + 12.0f, 1, stone);
        slope_at(chunk_for(nave.cx, nave.cy), &nave, -11.3f, 11.3f, -6.5f, h + 6.6f, 0, h + 12.0f, slate);
        slope_at(chunk_for(nave.cx, nave.cy), &nave, -11.3f, 11.3f, 6.5f, h + 6.6f, 0, h + 12.0f, slate);
        for (k = 0; k < 4; k++) {                    /* tall windows down its sides */
            float a = -7.5f + k * 5.0f;
            wall_at(chunk_for(nave.cx, nave.cy), &nave, 0, -6.03f, a - 0.8f, a + 0.8f, h + 1.8f, h + 5.8f, -1, glass, -1e9f);
            wall_at(chunk_for(nave.cx, nave.cy), &nave, 0, 6.03f, a - 0.8f, a + 0.8f, h + 1.8f, h + 5.8f, 1, glass, -1e9f);
        }
        fbox(chunk_for(tower.cx, tower.cy), &tower, 0, 0, 3.0f, 3.0f, h - 0.5f, h + 18.0f, stone, stone);
        for (k = 0; k < 4; k++) {                    /* the belfry's openings */
            bframe side = tower;
            float a = t->heading + k * WG_PI / 2;
            side.c = cosf(a);
            side.s = sinf(a);
            wall_at(chunk_for(tower.cx, tower.cy), &side, 0, -3.03f, -1.0f, 1.0f, h + 13.5f, h + 16.5f, -1, 0x2A2A34u, -1e9f);
        }
        wall_at(chunk_for(tower.cx, tower.cy), &tower, 0, -3.03f, -1.0f, 1.0f, h, h + 3.4f, -1, 0x6A4028u, -1e9f);
        cone(tower.cx, tower.cy, h + 18.0f, h + 30.0f, 3.4f, 8, copper);
        variant = WG_HIGH_ONLY;
        clock_face(&tower, 0, -3.06f, h + 10.5f, 1.4f);
        blob(tower.cx, tower.cy, h + 30.3f, 0.35f, 0.35f, 0xF2C230u);
        (void)x0;
        (void)y0;
        break;
    }
    case 1: {
        /* A water tower: a round tank on four legs, a cone of a roof, a red band round it. */
        const uint32_t tank = 0xBFE0F0u, legs = 0x8A8F99u;
        variant = WG_LOW_ONLY;
        block(f.cx, f.cy, 0.8f, 0.8f, t->heading, 14.0f, legs, legs, 0, 0);
        fbox(m, &f, 0, 0, 5.0f, 5.0f, h + 14.0f, h + 20.5f, tank, tank);
        pyramid(f.cx, f.cy, t->heading, 5.0f, h + 20.5f, h + 23.5f, 0xE8402Au);
        add_box(f.cx, f.cy, 4.0f, 4.0f, t->heading);
        variant = WG_MEDIUM_UP;
        for (k = 0; k < 4; k++) {
            bframe l = sub_frame(&f, k & 1 ? 3.5f : -3.5f, k & 2 ? 3.5f : -3.5f);
            trunk(l.cx, l.cy, h - 0.3f, h + 14.2f, 0.35f, 6, legs);
        }
        frustum(f.cx, f.cy, h + 13.6f, h + 14.2f, 3.5f, 5.6f, 14, legs);
        trunk(f.cx, f.cy, h + 14.2f, h + 17.0f, 5.6f, 14, tank);
        trunk(f.cx, f.cy, h + 17.0f, h + 18.0f, 5.62f, 14, 0xE8402Au);
        trunk(f.cx, f.cy, h + 18.0f, h + 20.5f, 5.6f, 14, tank);
        cone(f.cx, f.cy, h + 20.5f, h + 23.5f, 6.0f, 14, 0xE8402Au);
        variant = WG_HIGH_ONLY;                      /* braces between the legs */
        for (k = 0; k < 4; k++) {
            bframe side = f;
            float a = t->heading + k * WG_PI / 2;
            side.c = cosf(a);
            side.s = sinf(a);
            wall_at(m, &side, 0, -3.5f, -3.5f, 3.5f, h + 6.8f, h + 7.2f, -1, legs, -1e9f);
            wall_at(m, &side, 0, -3.5f, -3.5f, 3.5f, h + 10.8f, h + 11.2f, -1, legs, -1e9f);
        }
        blob(f.cx, f.cy, h + 23.7f, 0.4f, 0.4f, 0xF2C230u);
        break;
    }
    case 2: {
        /* A town hall: cream walls, a hipped roof, a portico of columns, a clock tower on top. */
        const uint32_t cream = 0xF0E2C0u, roof = 0x7A3A2Au, white = 0xF6F4EEu;
        bframe hall = sub_frame(&f, 0, 2.0f);
        variant = WG_LOW_ONLY;
        block(hall.cx, hall.cy, 12.0f, 7.0f, t->heading, 11.0f, cream, roof, 1, 1);
        fbox(chunk_for(hall.cx, hall.cy), &hall, 0, 0, 3.0f, 3.0f, h + 11.0f, h + 20.0f, cream, cream);
        pyramid(hall.cx, hall.cy, t->heading, 3.3f, h + 20.0f, h + 24.0f, roof);
        variant = WG_MEDIUM_UP;
        m = chunk_for(hall.cx, hall.cy);
        bshadow(&hall, 12.0f, 7.0f, 11.0f);
        fbox(m, &hall, 0, 0, 12.0f, 7.0f, h - 0.5f, h + 9.0f, cream, roof);
        slope_at(m, &hall, -12.4f, 12.4f, -7.4f, h + 8.8f, -2.0f, h + 12.0f, roof);
        slope_at(m, &hall, -12.4f, 12.4f, 7.4f, h + 8.8f, 2.0f, h + 12.0f, roof);
        top_at(m, &hall, -12.4f, 12.4f, -2.0f, 2.0f, h + 12.0f, roof);
        for (k = 0; k < 5; k++) {                    /* windows, two floors */
            float a = -10.0f + k * 5.0f;
            wall_at(m, &hall, 0, -7.03f, a - 0.8f, a + 0.8f, h + 1.2f, h + 3.4f, -1, 0x8FCBEFu, -1e9f);
            wall_at(m, &hall, 0, -7.03f, a - 0.8f, a + 0.8f, h + 5.0f, h + 7.4f, -1, 0x8FCBEFu, -1e9f);
        }
        for (k = 0; k < 4; k++) {                    /* the portico: four columns, a pediment */
            bframe col = sub_frame(&hall, -3.6f + k * 2.4f, -8.4f);
            trunk(col.cx, col.cy, h, h + 6.0f, 0.4f, 6, white);
        }
        fbox(m, &hall, 0, -8.2f, 4.6f, 1.3f, h + 6.0f, h + 6.8f, white, white);
        {
            float p[3][3], out[3];
            bpoint(&hall, -4.6f, -9.5f, h + 6.8f, p[0]);
            bpoint(&hall, 4.6f, -9.5f, h + 6.8f, p[1]);
            bpoint(&hall, 0, -9.5f, h + 8.6f, p[2]);
            fdir(&hall, 1, -1, out);
            face(m, p, 3, out, white, -1e9f);
            slope_at(m, &hall, -4.8f, 4.8f, -9.5f, h + 6.8f, -9.5f, h + 8.6f, roof);
        }
        fbox(m, &hall, 0, 0, 3.0f, 3.0f, h + 9.0f, h + 20.0f, cream, cream);
        cone(hall.cx, hall.cy, h + 20.0f, h + 24.0f, 3.6f, 4, roof);
        for (k = 0; k < 4; k++) {
            bframe side = hall;
            float a = t->heading + k * WG_PI / 2;
            side.c = cosf(a);
            side.s = sinf(a);
            clock_face(&side, 0, -3.04f, h + 16.5f, 1.7f);
        }
        variant = WG_HIGH_ONLY;
        blob(hall.cx, hall.cy, h + 24.3f, 0.4f, 0.4f, 0xF2C230u);
        fbox(m, &hall, 0, -10.6f, 4.0f, 1.0f, h - 0.3f, h + 0.25f, 0xD8D0C0u, 0xE6E0D4u);   /* steps */
        break;
    }
    default: {
        /* A windmill: a round white tower, a dark cap, four sails facing the street. */
        const uint32_t white = 0xF4F0E6u, cap = 0x9A3A2Au, sail = 0xF6F4EEu, wood = 0x7A5230u;
        bframe hub = sub_frame(&f, 0, -4.2f);
        float p[4][3], out[3];
        variant = WG_LOW_ONLY;
        block(f.cx, f.cy, 4.0f, 4.0f, t->heading, 14.0f, white, white, 1, 1);
        pyramid(f.cx, f.cy, t->heading, 4.0f, h + 14.0f, h + 18.0f, cap);
        variant = WG_MEDIUM_UP;
        frustum(f.cx, f.cy, h - 0.3f, h + 14.0f, 5.0f, 3.4f, 12, white);
        cone(f.cx, f.cy, h + 13.8f, h + 18.0f, 3.9f, 12, cap);
        wall_at(m, &f, 0, -4.86f, -0.9f, 0.9f, h, h + 2.4f, -1, 0x6A4028u, -1e9f);
        wall_at(m, &f, 0, -4.4f, -0.6f, 0.6f, h + 7.0f, h + 8.4f, -1, 0x8FCBEFu, -1e9f);
        for (k = 0; k < 4; k++) {                    /* the sails, a cross turned a little, both LOW and up */
            float a = 0.35f + k * WG_PI / 2, ca = cosf(a), sa = sinf(a), hy = h + 14.0f;
            int pass;
            for (pass = 0; pass < 2; pass++) {
                variant = pass ? WG_MEDIUM_UP : WG_LOW_ONLY;
                bpoint(&hub, 1.0f * ca - 0.8f * sa, 0, hy + 1.0f * sa + 0.8f * ca, p[0]);
                bpoint(&hub, 10.0f * ca - 0.8f * sa, 0, hy + 10.0f * sa + 0.8f * ca, p[1]);
                bpoint(&hub, 10.0f * ca + 0.8f * sa, 0, hy + 10.0f * sa - 0.8f * ca, p[2]);
                bpoint(&hub, 1.0f * ca + 0.8f * sa, 0, hy + 1.0f * sa - 0.8f * ca, p[3]);
                fdir(&f, 1, -1, out);
                face(chunk_for(hub.cx, hub.cy), p, 4, out, pass ? sail : white, -1e9f);
                fdir(&f, 1, 1, out);
                face(chunk_for(hub.cx, hub.cy), p, 4, out, pass ? sail : white, -1e9f);
            }
            variant = WG_MEDIUM_UP;                  /* the spar down its middle */
            bpoint(&hub, 0.0f * ca - 0.15f * sa, -0.05f, hy - 0.15f * ca, p[0]);
            bpoint(&hub, 10.5f * ca - 0.15f * sa, -0.05f, hy + 10.5f * sa + 0.15f * ca, p[1]);
            bpoint(&hub, 10.5f * ca + 0.15f * sa, -0.05f, hy + 10.5f * sa - 0.15f * ca, p[2]);
            bpoint(&hub, 0.0f * ca + 0.15f * sa, -0.05f, hy + 0.15f * ca, p[3]);
            fdir(&f, 1, -1, out);
            face(chunk_for(hub.cx, hub.cy), p, 4, out, wood, -1e9f);
        }
        blob(hub.cx, hub.cy, h + 14.0f, 0.6f, 0.6f, wood);
        break;
    }
    }
    variant = WG_SHARED;
    (void)x;
    (void)y;
}

/* A street of town t along u (across = 0) or v (across = 1) at `at`,
 * from -l to l; the two through the middle stop at the ring. */
static void town_street(const place *t, int across, float at, float l, float lift)
{
    float x0, y0, x1, y1, cut = at == 0 ? WG_RING_R : 0;
    int half;
    for (half = 0; half < (cut > 0 ? 2 : 1); half++) {
        float a = cut > 0 ? (half ? cut : -l) : -l, b = cut > 0 ? (half ? l : -cut) : l;
        if (across) {
            town_frame(t, a, at, &x0, &y0);
            town_frame(t, b, at, &x1, &y1);
        } else {
            town_frame(t, at, a, &x0, &y0);
            town_frame(t, at, b, &x1, &y1);
        }
        street(x0, y0, x1, y1, t->h, lift);
    }
}

/* The roundabout in town t's middle: the ring (every detail), an island
 * kerbed red and white with grass on top and a tree, solid to the kerb
 * (four boxes, an octagon). */
static void roundabout(const place *t)
{
    enum { SEGS = 32 };
    float cx = t->x, cy = t->y, h = t->h, r0 = WG_RING_R - 4.5f, r1 = WG_RING_R + 4.5f, ri = WG_RING_ISLAND;
    float apothem = ri * cosf(WG_PI / 8);
    int k;
    for (k = 0; k < SEGS; k++) {
        float a0 = 2 * WG_PI * k / SEGS, a1 = 2 * WG_PI * (k + 1) / SEGS;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        /* The ring's surface, a little above the streets and below the roads that meet it. */
        float q[4][3] = { { cx + r0 * c0, h + 0.14f, -(cy + r0 * s0) }, { cx + r1 * c0, h + 0.14f, -(cy + r1 * s0) },
                          { cx + r1 * c1, h + 0.14f, -(cy + r1 * s1) }, { cx + r0 * c1, h + 0.14f, -(cy + r0 * s1) } };
        float kb[4][3] = { { cx + ri * c0, h + 0.1f, -(cy + ri * s0) }, { cx + ri * c1, h + 0.1f, -(cy + ri * s1) },
                           { cx + ri * c1, h + 0.55f, -(cy + ri * s1) }, { cx + ri * c0, h + 0.55f, -(cy + ri * s0) } };
        float rg = ri - 1.0f;                     /* the grass, inside the kerb's top */
        float top[3][3] = { { cx, h + 0.55f, -cy }, { cx + rg * c1, h + 0.55f, -(cy + rg * s1) },
                            { cx + rg * c0, h + 0.55f, -(cy + rg * s0) } };
        float kt[4][3] = { { cx + rg * c0, h + 0.55f, -(cy + rg * s0) }, { cx + ri * c0, h + 0.55f, -(cy + ri * s0) },
                           { cx + ri * c1, h + 0.55f, -(cy + ri * s1) }, { cx + rg * c1, h + 0.55f, -(cy + rg * s1) } };
        uint32_t kerb = (k & 1) ? 0xF4F4F0u : 0xD8402Eu;
        tex_tile = WG_TX_ASPHALT;
        tex_quad = 1;
        quad(q[0], q[1], q[2], q[3], 0x565B66u, 0.92f);
        tex_tile = WG_TX_PLAIN;
        tex_quad = 0;
        quad(kb[0], kb[1], kb[2], kb[3], kerb, -1);
        quad(kt[0], kt[1], kt[2], kt[3], kerb, -1);
        quad(top[0], top[2], top[1], top[1], 0x6CC24Au, -1);
        {
            /* MEDIUM and HIGH: a white line along the ring's inner edge. */
            float l0 = r0 + 0.3f, l1 = r0 + 0.5f;
            float m[4][3] = { { cx + l0 * c0, h + 0.2f, -(cy + l0 * s0) }, { cx + l1 * c0, h + 0.2f, -(cy + l1 * s0) },
                              { cx + l1 * c1, h + 0.2f, -(cy + l1 * s1) }, { cx + l0 * c1, h + 0.2f, -(cy + l0 * s1) } };
            variant = WG_MEDIUM_UP;
            quad(m[0], m[1], m[2], m[3], 0xF2F2EAu, 1.0f);
            variant = WG_SHARED;
        }
    }
    for (k = 0; k < 4; k++)
        add_box(cx, cy, apothem, apothem * tanf(WG_PI / 8), t->heading + k * WG_PI / 4);
    /* The tree in the middle: a block at LOW, round at MEDIUM and HIGH, with a ring of round bushes. */
    variant = WG_LOW_ONLY;
    block(cx, cy, 0.5f, 0.5f, 0, 3.6f, 0x7A5230u, 0x7A5230u, 0, 0);
    {
        float s = 3.4f, c0 = h + 3.0f, c1 = c0 + 5.0f, x = cx, y = cy;
        float v[8][3] = { { x - s, c0, -(y - s) }, { x + s, c0, -(y - s) }, { x + s, c0, -(y + s) },
                          { x - s, c0, -(y + s) }, { x - s, c1, -(y - s) }, { x + s, c1, -(y - s) },
                          { x + s, c1, -(y + s) }, { x - s, c1, -(y + s) } };
        quad(v[4], v[5], v[6], v[7], 0x4CB040u, -1);
        quad(v[0], v[1], v[5], v[4], 0x4CB040u, -1);
        quad(v[1], v[2], v[6], v[5], 0x4CB040u, -1);
        quad(v[2], v[3], v[7], v[6], 0x4CB040u, -1);
        quad(v[3], v[0], v[4], v[7], 0x4CB040u, -1);
    }
    variant = WG_MEDIUM_UP;
    trunk(cx, cy, h + 0.4f, h + 4.0f, 0.6f, 6, 0x7A5230u);
    blob(cx, cy, h + 7.0f, 6.0f, 4.6f, 0x5DBB3Fu);
    for (k = 0; k < 8; k++) {                    /* round bushes, by turns yellow-green and deep green */
        float a = t->heading + WG_PI / 8 + k * WG_PI / 4, r = ri - 4.0f;
        blob(cx + r * cosf(a), cy + r * sinf(a), h + 1.4f, 2.2f, 1.5f, (k & 1) ? 0x8FCF3Au : 0x3E9E3Au);
    }
    variant = WG_SHARED;
}

static void build_town(const place *t)
{
    int n = town_blocks(t), i, j;
    for (i = -n; i <= n; i++) {                   /* streets */
        town_street(t, 0, i * GRID, n * GRID, 0.12f);
        town_street(t, 1, i * GRID, n * GRID, 0.13f);     /* crossings: not coplanar */
    }
    roundabout(t);
    {
        /* HIGH: lamp posts along the streets every 48 m, by turns either side, off the main roads. */
        int a, q, nq = (int)(n * GRID / 48.0f);
        variant = WG_HIGH_ONLY;
        for (i = -n; i <= n; i++)
            for (a = 0; a < 2; a++)
                for (q = -nq; q < nq; q++) {
                    float u = (q + 0.5f) * 48.0f, side = (q & 1) ? 5.8f : -5.8f, lx, ly, ix, iy;
                    if (a == 0) {
                        town_frame(t, i * GRID + side, u, &lx, &ly);
                        town_frame(t, i * GRID + side * 0.8f, u, &ix, &iy);
                    } else {
                        town_frame(t, u, i * GRID + side, &lx, &ly);
                        town_frame(t, u, i * GRID + side * 0.8f, &ix, &iy);
                    }
                    if (road_distance(lx, ly) < 8.0f)
                        continue;
                    lamp(lx, ly, 5.0f, ix - lx, iy - ly);
                }
        variant = WG_SHARED;
    }
    {
        /* The blocks inside the town: shops in the middle four (round the roundabout); of the others the
         * landmark in the best (near the middle, clear of main roads), the park in the next not beside it;
         * terraces in the rest, along all four sides. */
        int mark_i = 99, mark_j = 99, park_i = 99, park_j = 99, pass;
        for (pass = 0; pass < 2; pass++) {
            float best = -1e9f;
            int bi = 99, bj = 99;
            for (j = -n; j < n; j++)
                for (i = -n; i < n; i++) {
                    float bu = (i + 0.5f) * GRID, bv = (j + 0.5f) * GRID, cx, cy, clear, score;
                    if ((i == -1 || i == 0) && (j == -1 || j == 0))
                        continue;
                    if (sqrtf(bu * bu + bv * bv) > t->r || (i == mark_i && j == mark_j))
                        continue;
                    if (pass && abs(i - mark_i) <= 1 && abs(j - mark_j) <= 1)
                        continue;
                    town_frame(t, bu, bv, &cx, &cy);
                    clear = DGK_MIN(road_distance(cx, cy), 40.0f);
                    score = clear * 2.0f - sqrtf(bu * bu + bv * bv) * 0.25f;
                    if (clear >= 22.0f && score > best) {
                        best = score;
                        bi = i;
                        bj = j;
                    }
                }
            if (pass) {
                park_i = bi;
                park_j = bj;
            } else {
                mark_i = bi;
                mark_j = bj;
            }
        }
        for (j = -n; j < n; j++)
            for (i = -n; i < n; i++) {
                float u0 = i * GRID, u1 = u0 + GRID, v0 = j * GRID, v1 = v0 + GRID, bu = u0 + GRID / 2, bv = v0 + GRID / 2;
                float half_ns = GRID / 2 - ROW_SET - ROW_DEPTH - 0.5f, half_ew = GRID / 2 - ROW_SET;
                int shops = (i == -1 || i == 0) && (j == -1 || j == 0), side;
                if (sqrtf(bu * bu + bv * bv) > t->r)
                    continue;
                pavements(t, u0, u1, v0, v1);
                if (i == park_i && j == park_j) {
                    park(t, u0, u1, v0, v1);
                    block_hedge(t, u0, u1, v0, v1, 0);
                    continue;
                }
                if (i == mark_i && j == mark_j) {
                    landmark(t, u0, u1, v0, v1, (int)(t - places));
                    block_hedge(t, u0, u1, v0, v1, (u0 + u1) * 0.5f);
                    continue;
                }
                for (side = 0; side < 4; side++) {
                    /* The row facing the street at v0, v1, u0, u1: its frame turned so that its front is -v. */
                    static const float turn[4] = { 0.0f, WG_PI, -WG_PI / 2, WG_PI / 2 };
                    bframe f;
                    float ru = side < 2 ? bu : side == 2 ? u0 + ROW_SET + ROW_DEPTH / 2 : u1 - ROW_SET - ROW_DEPTH / 2;
                    float rv = side >= 2 ? bv : side == 0 ? v0 + ROW_SET + ROW_DEPTH / 2 : v1 - ROW_SET - ROW_DEPTH / 2;
                    float half = side < 2 ? half_ns : half_ew;
                    f.c = cosf(t->heading + turn[side]);
                    f.s = sinf(t->heading + turn[side]);
                    town_frame(t, ru, rv, &f.cx, &f.cy);
                    terrace_row(t, &f, -half, half, shops);
                }
                if (!shops)
                    garden_hedges(t, u0, u1, v0, v1);
            }
        {
            static const char *const kinds[4] = { "a church", "a water tower", "a town hall", "a windmill" };
            float mx, my, px, py;
            town_frame(t, (mark_i + 0.5f) * GRID, (mark_j + 0.5f) * GRID, &mx, &my);
            town_frame(t, (park_i + 0.5f) * GRID, (park_j + 0.5f) * GRID, &px, &py);
            printf("fwgen: town %d: %s at (%.0f, %.0f), the park at (%.0f, %.0f), heading %.0f\n", (int)(t - places),
                   kinds[(int)(t - places) % 4], mx, my, px, py, t->heading * 57.29578f);
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
    float x0, y0, x1, y1;
    int k;
    for (k = -2; k <= 2; k++) {
        depot_frame(d, -48, k * 16.0f, &x0, &y0);
        depot_frame(d, 48, k * 16.0f, &x1, &y1);
        level_segment(x0, y0, d->h, x1, y1, d->h, 20.0f, 30.0f);
    }
}

static void hedge_stretch(float ax, float ay, float ah, float bx, float by, float bh, uint32_t col);
static void trunk(float x, float y, float z0, float z1, float r, int n, uint32_t rgb);

/* A lamp post for MEDIUM and HIGH: a slim pole to `tall`, an arm and a lit
 * head, at (x, y) on the ground; nothing collides with it. */
static void lamp(float x, float y, float tall, float ax, float ay)
{
    float g = ground(x, y), hx = x + ax, hy = y + ay, p[4][3], up[3] = { 0, 1, 0 }, down[3] = { 0, -1, 0 };
    cmesh *m = chunk_for(x, y);
    trunk(x, y, g - 0.2f, g + tall, 0.09f, 4, 0x55606Cu);
    p[0][0] = hx - 0.35f; p[0][1] = g + tall - 0.1f; p[0][2] = -(hy - 0.35f);
    p[1][0] = hx + 0.35f; p[1][1] = g + tall - 0.1f; p[1][2] = -(hy - 0.35f);
    p[2][0] = hx + 0.35f; p[2][1] = g + tall - 0.1f; p[2][2] = -(hy + 0.35f);
    p[3][0] = hx - 0.35f; p[3][1] = g + tall - 0.1f; p[3][2] = -(hy + 0.35f);
    face(m, p, 4, down, 0xFFF4C0u, -1e9f);         /* the lamp, lit, seen from below and beside */
    p[0][1] = p[1][1] = p[2][1] = p[3][1] = g + tall + 0.15f;
    face(m, p, 4, up, 0x45505Cu, -1e9f);
}

static void build_depot(const place *d, wg_depot *out, int number)
{
    float h = d->h, c[4][2], x, y;
    static const float cu[4] = { -48, 48, 48, -48 }, cv[4] = { -35, -35, 35, 35 };
    int i, k;
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
    variant = WG_LOW_ONLY;
    block(x, y, 42, 10, d->heading, 7, 0xE4E0D6u, 0x3A6FD8u, 1, 1);
    variant = WG_MEDIUM_UP;
    warehouse(x, y, 42, 10, d->heading, 7, 0xE4E0D6u, 0x3A6FD8u);
    variant = WG_SHARED;
    for (i = 0; i < WG_BAYS; i++) {
        float bx, by, u = (i - 1) * 16.0f, px, py;
        depot_frame(d, u, 34.2f, &bx, &by);                 /* a dark door on the warehouse face */
        block(bx, by, 2.0f, 0.4f, d->heading, 4.2f, 0x2A2A33u, 0x2A2A33u, 0, 0);
        depot_frame(d, u - 7.5f, 31, &px, &py);             /* the dock between bays */
        block(px, py, 4.0f, 3.0f, d->heading, 1.3f, 0xC8C8D0u, 0xB0B0B8u, 0, 1);
        for (k = -1; k <= 1; k += 2) {                       /* bold white bay lines, in the depot's frame */
            float q[4][2], lu = u + k * 1.6f;
            depot_frame(d, lu - 0.15f, 14, &q[0][0], &q[0][1]);
            depot_frame(d, lu + 0.15f, 14, &q[1][0], &q[1][1]);
            depot_frame(d, lu + 0.15f, 34, &q[2][0], &q[2][1]);
            depot_frame(d, lu - 0.15f, 34, &q[3][0], &q[3][1]);
            {
                float p[4][3];
                int m;
                for (m = 0; m < 4; m++) {
                    p[m][0] = q[m][0];
                    p[m][1] = h + 0.2f;
                    p[m][2] = -q[m][1];
                }
                quad(p[0], p[1], p[2], p[3], 0xF4F4F0u, 1.0f);
            }
        }
        /* Docked: the trailer's rear at the dock face, pointing out of the bay. */
        depot_frame(d, u, 33.5f, &out->bay[i][0], &out->bay[i][1]);
        out->bay[i][2] = d->heading - WG_PI / 2;
    }
    {
        /* Each bay's number painted in it, between its lines, where the reversing camera still sees it with a
         * trailer half in (every detail: it says which bay is which), and the depot's on the warehouse's face,
         * left of the docks, as the HUD numbers them. */
        float c = cosf(d->heading), sn = sinf(d->heading), o[3], right[3], down[3], up[3] = { 0, 1, 0 }, x0, y0;
        right[0] = c; right[1] = 0; right[2] = -sn;
        down[0] = sn; down[1] = 0; down[2] = c;
        for (i = 0; i < WG_BAYS; i++) {
            depot_frame(d, (i - 1) * 16.0f - 1.05f, 30.0f, &x0, &y0);
            o[0] = x0; o[1] = h + 0.2f; o[2] = -y0;
            paint_number(i + 1, o, right, down, up, 0.42f, 0xF4F4F0u);
        }
        {
            /* On the wall, facing the apron: the depot's number, and each bay's above its door (where the
             * reversing camera looks). */
            float wdown[3] = { 0, -1, 0 }, wout[3];
            wout[0] = sn; wout[1] = 0; wout[2] = c;
            depot_frame(d, -33.0f, 34.95f, &x0, &y0);
            o[0] = x0; o[1] = h + 6.6f; o[2] = -y0;
            paint_number(number, o, right, wdown, wout, 0.34f, 0x2A4A8Au);
            for (i = 0; i < WG_BAYS; i++) {
                depot_frame(d, (i - 1) * 16.0f - 0.7f, 34.95f, &x0, &y0);
                o[0] = x0; o[1] = h + 6.5f; o[2] = -y0;
                paint_number(i + 1, o, right, wdown, wout, 0.28f, 0xF2B01Eu);
            }
        }
    }
    {
        /* MEDIUM and HIGH: a hedge round the yard, open at the gate, and lamps at the apron's corners. */
        static const float corner[5][2] = { { -8, -50 }, { -62, -50 }, { -62, 57 }, { 62, 57 }, { 62, -50 } };
        int e;
        variant = WG_MEDIUM_UP;
        for (e = 0; e < 5; e++) {
            float a[2], b[2], l;
            int n, q;
            a[0] = corner[e][0];
            a[1] = corner[e][1];
            b[0] = e < 4 ? corner[e + 1][0] : 8.0f;
            b[1] = e < 4 ? corner[e + 1][1] : -50.0f;
            l = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
            n = (int)ceilf(l / 16.0f);
            for (q = 0; q < n; q++) {
                float ax, ay, bx, by;
                depot_frame(d, a[0] + (b[0] - a[0]) * q / n, a[1] + (b[1] - a[1]) * q / n, &ax, &ay);
                depot_frame(d, a[0] + (b[0] - a[0]) * (q + 1) / n, a[1] + (b[1] - a[1]) * (q + 1) / n, &bx, &by);
                hedge_stretch(ax, ay, ground(ax, ay), bx, by, ground(bx, by), 0x3E8C36u);
            }
        }
        for (k = 0; k < 4; k++) {
            float lx, ly, ix, iy;
            depot_frame(d, (k & 1) ? 47.0f : -47.0f, (k & 2) ? 30.0f : -36.0f, &lx, &ly);
            depot_frame(d, (k & 1) ? 46.0f : -46.0f, (k & 2) ? 29.0f : -35.0f, &ix, &iy);
            lamp(lx, ly, 6.5f, (ix - lx) * 1.2f, (iy - ly) * 1.2f);
        }
        variant = WG_SHARED;
    }
    /* A trailer waiting at the apron's left edge, nose to the right (+u):
     * the rest of the apron stays clear for turning round to a bay. */
    depot_frame(d, -30, -22, &out->pickup[0], &out->pickup[1]);
    out->pickup[2] = d->heading;
    out->x = d->x;
    out->y = d->y;
    out->heading = d->heading;
}

/* The ground: 16 m cells, each quad coloured by the field it lies in
 * (a patchwork of crops around Voronoi seeds), grass in towns. */
#define FIELDS 700
static float field_seed[FIELDS][2];
static uint8_t field_crop[FIELDS];

/* The ground's regions: a place's grass (a disc, radius r + 30), else the
 * field of the nearest seed. A region: -1 - place, or the field's index. */
static int region_at(float x, float y)
{
    int i, best = 0;
    float bd = 1e30f;
    for (i = 0; i < nplaces; i++) {
        float dx = x - places[i].x, dy = y - places[i].y;
        if (dx * dx + dy * dy < (places[i].r + 30) * (places[i].r + 30))
            return -1 - i;
    }
    for (i = 0; i < FIELDS; i++) {
        float dx = x - field_seed[i][0], dy = y - field_seed[i][1], d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static uint32_t region_colour(int r)
{
    return r < 0 ? 0x7CCB55u : crops[field_crop[r]];       /* town and depot grass; a crop */
}

static uint32_t ground_colour(float x, float y)
{
    return region_colour(region_at(x, y));
}

/* Negative on region a's side of its boundary with region b, positive on
 * b's: between two fields the bisector of their seeds (a straight line);
 * round a place, its disc (a circle: cut by chords, 2 cm off at most). */
static float region_split(int a, int b, float x, float y)
{
    if (a < 0) {
        const place *p = &places[-1 - a];
        return (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) - (p->r + 30) * (p->r + 30);
    }
    if (b < 0) {
        const place *p = &places[-1 - b];
        return (p->r + 30) * (p->r + 30) - (x - p->x) * (x - p->x) - (y - p->y) * (y - p->y);
    }
    return (x - field_seed[a][0]) * (x - field_seed[a][0]) + (y - field_seed[a][1]) * (y - field_seed[a][1]) -
           (x - field_seed[b][0]) * (x - field_seed[b][0]) - (y - field_seed[b][1]) * (y - field_seed[b][1]);
}

/* The terrain fitted under everything laid flat on it: the roads, each
 * town's streets, each depot's apron (in strips 5 m wide) and its gate's
 * stub. Every strip is filled first and cut after, so a fill for one strip
 * never raises the ground through another. */
static void fit_surfaces(void)
{
    int e, i, k, fill;
    for (fill = 1; fill >= 0; fill--) {
        for (e = 0; e < nedges; e++)
            for (i = (int)edges[e].first_point; i + 1 < (int)(edges[e].first_point + edges[e].npoints); i++)
                fit_strip(pts[i][0], pts[i][1], pts[i][2], pts[i + 1][0], pts[i + 1][1], pts[i + 1][2], 4.5f, fill);
        for (k = 0; k < nplaces; k++) {
            const place *p = &places[k];
            float x0, y0, x1, y1;
            if (p->kind == WG_TOWN) {
                int n = town_blocks(p);
                for (i = -n; i <= n; i++) {
                    town_frame(p, i * GRID, -n * GRID, &x0, &y0);
                    town_frame(p, i * GRID, n * GRID, &x1, &y1);
                    fit_strip(x0, y0, p->h, x1, y1, p->h, 4.5f, fill);
                    town_frame(p, -n * GRID, i * GRID, &x0, &y0);
                    town_frame(p, n * GRID, i * GRID, &x1, &y1);
                    fit_strip(x0, y0, p->h, x1, y1, p->h, 4.5f, fill);
                }
                continue;
            }
            for (i = 0; i < 15; i++) {
                depot_frame(p, -48, -35 + i * 5.0f, &x0, &y0);
                depot_frame(p, 48, -35 + i * 5.0f, &x1, &y1);
                fit_strip(x0, y0, p->h, x1, y1, p->h, 2.5f, fill);
            }
            depot_frame(p, 0, -52, &x0, &y0);
            depot_frame(p, 0, -34, &x1, &y1);
            fit_strip(x0, y0, p->h, x1, y1, p->h, 4.5f, fill);
        }
    }
}

/* The sun's light on the terrain at sample (i, j), from its own normal (the
 * height field's slope across the samples either side): smooth shading. */
static float terrain_light(int i, int j)
{
    float sx = (h_at(i + 1, j) - h_at(i - 1, j)) / (2 * WG_CELL), sy = (h_at(i, j + 1) - h_at(i, j - 1)) / (2 * WG_CELL);
    float l = sqrtf(sx * sx + 1 + sy * sy);
    return mb_light(-sx / l, 1 / l, sy / l);
}

/* A point of the terrain: ground x, y, its height and its light. */
typedef struct tvert {
    float x, y, h, light;
} tvert;

/* Where f (f(p) < 0 < f(q)) crosses p-q, worked out from whichever end
 * comes first (x, then y): the cells either side of an edge cross it at
 * exactly the same point, so no crack opens between them. */
static tvert crossing(tvert p, float fp, tvert q, float fq)
{
    tvert r;
    float t;
    if (q.x < p.x || (q.x == p.x && q.y < p.y)) {
        tvert s = p;
        float fs = fp;
        p = q;
        q = s;
        fp = fq;
        fq = fs;
    }
    t = fp / (fp - fq);
    r.x = p.x + (q.x - p.x) * t;
    r.y = p.y + (q.y - p.y) * t;
    r.h = p.h + (q.h - p.h) * t;
    r.light = p.light + (q.light - p.light) * t;
    return r;
}

/* The boundaries the cuts found between two fields: hedges go along them. */
#define MAX_HEDGE 60000
static tvert hedge[MAX_HEDGE][2];
static int hedge_pair[MAX_HEDGE], nhedge;

/* A stretch of boundary between fields fa and fb, from p to q. One that
 * carries on from the last (the same two fields, and an end in common: a
 * cell's two triangles cut one straight boundary in turn) lengthens it. */
static void add_hedge(int fa, int fb, tvert p, tvert q)
{
    int pair = fa < fb ? fa * FIELDS + fb : fb * FIELDS + fa, e;
    if (nhedge && hedge_pair[nhedge - 1] == pair)
        for (e = 0; e < 2; e++) {
            tvert *end = &hedge[nhedge - 1][e];
            if (end->x == p.x && end->y == p.y) {
                *end = q;
                return;
            }
            if (end->x == q.x && end->y == q.y) {
                *end = p;
                return;
            }
        }
    if (nhedge < MAX_HEDGE) {
        hedge[nhedge][0] = p;
        hedge[nhedge][1] = q;
        hedge_pair[nhedge++] = pair;
    }
}

/* A convex piece of a terrain triangle, cut along the regions' boundaries
 * until each piece lies in one region's colour; each a fan of its own
 * vertices. The pieces lie on the triangle (heights interpolated), so the
 * ground is where it was. */
/* HIGH's tile for the ground of region r: a town's or a depot's grass, or a field's crops, their rows one of four
 * ways (by the field). */
static int ground_tile(int r)
{
    return r < 0 ? WG_TX_GRASS : WG_TX_CROPS + (int)(((uint32_t)r * 2654435761u >> 13) & 3);
}

static void terrain_piece(cmesh *c, const tvert *v, int n, int depth)
{
    tvert a[12], b[12];
    int reg[12], i, k, na = 0, nb = 0;
    float f[12], mx = 0, my = 0;
    /* Each corner's region, a little inside the piece: a corner made by a
     * cut lies on the boundary itself, where either region might answer. */
    for (i = 0; i < n; i++) {
        mx += v[i].x / n;
        my += v[i].y / n;
    }
    for (i = 0; i < n; i++)
        reg[i] = region_at(v[i].x + (mx - v[i].x) * 0.01f, v[i].y + (my - v[i].y) * 0.01f);
    for (k = 1; k < n && region_colour(reg[k]) == region_colour(reg[0]); k++)
        continue;
    if (k == n || depth > 6 || n > 9) {
        float cx = 0, cy = 0;
        int first;
        for (i = 0; i < n; i++) {
            cx += v[i].x / n;
            cy += v[i].y / n;
        }
        first = c->nverts;
        tex_tile = ground_tile(k == n ? reg[0] : region_at(cx, cy));   /* HIGH: the piece's 16 m cell over a tile */
        for (i = 0; i < n; i++) {
            float p[3];
            p[0] = v[i].x;
            p[1] = v[i].h;
            p[2] = -v[i].y;
            tex_u = (v[i].x - floorf(cx / WG_CELL) * WG_CELL) / WG_CELL;
            tex_v = (v[i].y - floorf(cy / WG_CELL) * WG_CELL) / WG_CELL;
            cmesh_vertex(c, p, mb_shade(k == n ? region_colour(reg[0]) : ground_colour(cx, cy), v[i].light));
        }
        tex_tile = WG_TX_PLAIN;
        for (i = 1; i + 1 < n; i++)
            cmesh_tri(c, first, first + i, first + i + 1);
        return;
    }
    for (i = 0; i < n; i++)
        f[i] = region_split(reg[0], reg[k], v[i].x, v[i].y);
    {
        tvert cut[2];
        int ncut = 0;
        for (i = 0; i < n; i++) {                    /* Sutherland-Hodgman: a gets f <= 0, b gets f > 0 */
            int j = (i + 1) % n;
            if (f[i] <= 0)
                a[na++] = v[i];
            else
                b[nb++] = v[i];
            if ((f[i] <= 0) != (f[j] <= 0)) {
                tvert x = f[i] <= 0 ? crossing(v[i], f[i], v[j], f[j]) : crossing(v[j], f[j], v[i], f[i]);
                a[na++] = x;
                b[nb++] = x;
                if (ncut < 2)
                    cut[ncut++] = x;
            }
        }
        /* Between two fields (not round a place's grass): a stretch of hedge. */
        if (ncut == 2 && reg[0] >= 0 && reg[k] >= 0)
            add_hedge(reg[0], reg[k], cut[0], cut[1]);
    }
    if (na >= 3)
        terrain_piece(c, a, na, depth + 1);
    if (nb >= 3)
        terrain_piece(c, b, nb, depth + 1);
}

/* The terrain, smooth: each sample a vertex lit by its own normal, shared by
 * the cells around it that lie in the same field. A cell whose corners lie
 * in fields of different colours is cut along their boundary (straight
 * between fields, chords round a place's grass), so the patchwork's edges
 * run true rather than in 16 m steps. The cells' split is the one
 * world_height reads (0-1-2, 0-2-3). */
#define CHUNK_CELLS 4                    /* WG_CHUNK / WG_CELL: checked in build_terrain */

static void build_terrain(void)
{
    int i, j, cx, cy;
    if (CHUNK_CELLS * WG_CELL != WG_CHUNK) {
        fprintf(stderr, "fwgen: CHUNK_CELLS is not WG_CHUNK / WG_CELL\n");
        exit(1);
    }
    for (i = 0; i < FIELDS; i++) {
        field_seed[i][0] = frand() * WG_SIZE;
        field_seed[i][1] = frand() * WG_SIZE;
        field_crop[i] = (uint8_t)dgk_rng_below(&rng, DGK_ARRAY_LEN(crops));
    }
    for (cy = 0; cy < WG_CHUNKS; cy++)
        for (cx = 0; cx < WG_CHUNKS; cx++) {
            cmesh *c = &chunks[WG_SHARED][cy * WG_CHUNKS + cx];
            for (j = 0; j < CHUNK_CELLS; j++)
                for (i = 0; i < CHUNK_CELLS; i++) {
                    static const int corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                    int gi = cx * CHUNK_CELLS + i, gj = cy * CHUNK_CELLS + j, v[4], k;
                    uint32_t rgb = ground_colour((gi + 0.5f) * WG_CELL, (gj + 0.5f) * WG_CELL), at[4];
                    for (k = 0; k < 4; k++)
                        at[k] = ground_colour((gi + corner[k][0]) * WG_CELL, (gj + corner[k][1]) * WG_CELL);
                    if (at[1] != at[0] || at[2] != at[0] || at[3] != at[0]) {
                        tvert t[4];
                        for (k = 0; k < 4; k++) {
                            t[k].x = (gi + corner[k][0]) * WG_CELL;
                            t[k].y = (gj + corner[k][1]) * WG_CELL;
                            t[k].h = h_at(gi + corner[k][0], gj + corner[k][1]);
                            t[k].light = terrain_light(gi + corner[k][0], gj + corner[k][1]);
                        }
                        {
                            /* MEDIUM and HIGH: cut true. LOW: the cell whole, in its middle's colour (fewer triangles). */
                            tvert t1[3], t2[3];
                            cmesh *lo = &chunks[WG_LOW_ONLY][cy * WG_CHUNKS + cx];
                            int q[4];
                            t1[0] = t[0]; t1[1] = t[1]; t1[2] = t[2];
                            t2[0] = t[0]; t2[1] = t[2]; t2[2] = t[3];
                            terrain_piece(&chunks[WG_MEDIUM_UP][cy * WG_CHUNKS + cx], t1, 3, 0);
                            terrain_piece(&chunks[WG_MEDIUM_UP][cy * WG_CHUNKS + cx], t2, 3, 0);
                            for (k = 0; k < 4; k++) {
                                float pp[3];
                                pp[0] = t[k].x;
                                pp[1] = t[k].h;
                                pp[2] = -t[k].y;
                                q[k] = cmesh_vertex(lo, pp, mb_shade(rgb, t[k].light));
                            }
                            cmesh_tri(lo, q[0], q[1], q[2]);
                            cmesh_tri(lo, q[0], q[2], q[3]);
                        }
                        continue;
                    }
                    /* The cell's own four corners, a tile over it for HIGH (shared ones cannot map their
                     * neighbours' tiles too); WORLD.PAK's welder makes them one again (same place, colour). */
                    rgb = at[0];
                    tex_tile = ground_tile(region_at((gi + 0.5f) * WG_CELL, (gj + 0.5f) * WG_CELL));
                    for (k = 0; k < 4; k++) {
                        float p[3];
                        p[0] = (gi + corner[k][0]) * WG_CELL;
                        p[1] = h_at(gi + corner[k][0], gj + corner[k][1]);
                        p[2] = -(gj + corner[k][1]) * WG_CELL;
                        tex_u = (float)corner[k][0];
                        tex_v = (float)corner[k][1];
                        v[k] = cmesh_vertex(c, p, mb_shade(rgb, terrain_light(gi + corner[k][0], gj + corner[k][1])));
                    }
                    tex_tile = WG_TX_PLAIN;
                    cmesh_tri(c, v[0], v[1], v[2]);
                    cmesh_tri(c, v[0], v[2], v[3]);
                }
        }
}

/* A stretch of hedge from (ax, ay) at height ah to (bx, by) at bh: a mound
 * 1.5 m across and 1 m high, smooth-shaded (three edges, so 4 triangles),
 * into the current run. Nothing collides with it. */
#define HEDGE_EDGES 3
static void hedge_stretch(float ax, float ay, float ah, float bx, float by, float bh, uint32_t col)
{
    static const float prof[HEDGE_EDGES][2] = { { -0.75f, -0.2f }, { 0.0f, 1.0f }, { 0.75f, -0.2f } };
    static const float pn[HEDGE_EDGES][2] = { { -0.8f, 0.6f }, { 0.0f, 1.0f }, { 0.8f, 0.6f } };
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy), nx, ny;
    int va[HEDGE_EDGES], vb[HEDGE_EDGES], k;
    cmesh *c;
    if (l < 0.5f)
        return;
    nx = -dy / l;
    ny = dx / l;
    c = chunk_for((ax + bx) * 0.5f, (ay + by) * 0.5f);
    for (k = 0; k < HEDGE_EDGES; k++) {
        float p[3], n[3];
        n[0] = nx * pn[k][0];
        n[1] = pn[k][1];
        n[2] = -ny * pn[k][0];
        p[0] = ax + nx * prof[k][0];
        p[1] = ah + prof[k][1];
        p[2] = -(ay + ny * prof[k][0]);
        tex_tile = WG_TX_LEAVES;
        tex_u = 0;
        tex_v = k * 0.5f;
        va[k] = smooth_vertex(c, p, n, col, 0.8f);
        p[0] = bx + nx * prof[k][0];
        p[1] = bh + prof[k][1];
        p[2] = -(by + ny * prof[k][0]);
        tex_u = DGK_MIN(l / 8.0f, 1.0f);
        vb[k] = smooth_vertex(c, p, n, col, 0.8f);
        tex_tile = WG_TX_PLAIN;
    }
    for (k = 0; k + 1 < HEDGE_EDGES; k++) {
        cmesh_tri(c, va[k], vb[k], vb[k + 1]);
        cmesh_tri(c, va[k], vb[k + 1], va[k + 1]);
    }
}

/* Hedges along the fields' boundaries (MEDIUM and HIGH's run): on each
 * stretch the cuts found, kept off the roads (8 m) and the places' grass,
 * with a gap now and then. */
static void build_hedges(void)
{
    int h, made = 0;
    variant = WG_MEDIUM_UP;
    for (h = 0; h < nhedge; h++) {
        const tvert *a = &hedge[h][0], *b = &hedge[h][1];
        float mx = (a->x + b->x) * 0.5f, my = (a->y + b->y) * 0.5f;
        if ((b->x - a->x) * (b->x - a->x) + (b->y - a->y) * (b->y - a->y) < 0.25f)
            continue;
        if (road_distance(mx, my) < 8.0f || hash2((int)mx, (int)my, 91) % 9 == 0)
            continue;
        hedge_stretch(a->x, a->y, a->h, b->x, b->y, b->h,
                      (hash2((int)(mx / 40), (int)(my / 40), 92) & 1) ? 0x3A8A34u : 0x478F3Au);
        made++;
    }
    variant = WG_SHARED;
    printf("fwgen: %d stretches of hedge\n", made);
}

/* A roadside spot fit to plant: not on a place's grass, nor near another
 * road (or this one, closer than 6 m), nor in a depot's yard. */
static int verge_ok(float x, float y)
{
    int i;
    for (i = 0; i < nplaces; i++) {
        float dx = x - places[i].x, dy = y - places[i].y;
        if (dx * dx + dy * dy < (places[i].r + 30) * (places[i].r + 30))
            return 0;
    }
    return !in_depot(x, y) && road_distance(x, y) > 6.0f;
}

/* MEDIUM and HIGH's run: bushes along the roads' verges, now and then, by
 * a hash of where they are (the world's random sequence is left as it
 * was). Nothing collides with them. */
static void build_roadside(void)
{
    static const uint32_t bush[3] = { 0x3E9E3Au, 0x2F8A38u, 0x55B045u };
    int e, i, s, bushes = 0;
    variant = WG_MEDIUM_UP;
    for (e = 0; e < nedges; e++)
        for (i = (int)edges[e].first_point + 1; i + 1 < (int)(edges[e].first_point + edges[e].npoints); i++) {
            float dx = pts[i + 1][0] - pts[i - 1][0], dy = pts[i + 1][1] - pts[i - 1][1], l = sqrtf(dx * dx + dy * dy);
            float nx, ny;
            if (l < 0.01f)
                continue;
            nx = -dy / l;
            ny = dx / l;
            for (s = -1; s <= 1; s += 2) {
                uint32_t h = hash2(i * 2 + (s > 0), e, 301);
                if (h % 100 < 24) {                  /* a bush, or two */
                    float o = 7.5f + (h >> 8) % 25 / 10.0f, x = pts[i][0] + nx * o * s, y = pts[i][1] + ny * o * s;
                    float r = 0.8f + (h >> 16) % 6 / 10.0f;
                    if (verge_ok(x, y)) {
                        blob(x, y, ground(x, y) + r * 0.6f, r * 1.2f, r, bush[(h >> 20) % 3]);
                        bushes++;
                        if ((h >> 24) % 3 == 0) {
                            float x2 = x + dx / l * 1.6f, y2 = y + dy / l * 1.6f;
                            blob(x2, y2, ground(x2, y2) + r * 0.45f, r, r * 0.8f, bush[((h >> 20) + 1) % 3]);
                            bushes++;
                        }
                    }
                }
            }
        }
    variant = WG_SHARED;
    printf("fwgen: %d bushes\n", bushes);
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
        variant = WG_LOW_ONLY;
        if (dgk_rng_below(&rng, 3)) {                               /* lollipop: trunk and a chunky crown */
            float s = 1.6f + frand() * 1.2f;
            uint32_t col;
            block(x, y, 0.35f, 0.35f, 0, 2.2f + s * 0.4f, 0x7A5230u, 0x7A5230u, 0, 1);
            {
                float c0 = g + 1.8f, c1 = c0 + s * 2.2f;
                float v[8][3] = { { x - s, c0, -(y - s) }, { x + s, c0, -(y - s) }, { x + s, c0, -(y + s) },
                                  { x - s, c0, -(y + s) }, { x - s, c1, -(y - s) }, { x + s, c1, -(y - s) },
                                  { x + s, c1, -(y + s) }, { x - s, c1, -(y + s) } };
                col = leaves[dgk_rng_below(&rng, 4)];
                quad(v[4], v[5], v[6], v[7], col, -1);
                quad(v[0], v[1], v[5], v[4], col, -1);
                quad(v[1], v[2], v[6], v[5], col, -1);
                quad(v[2], v[3], v[7], v[6], col, -1);
                quad(v[3], v[0], v[4], v[7], col, -1);
            }
            /* MEDIUM and HIGH: a round crown, a little wider than tall, on a round trunk. */
            variant = WG_MEDIUM_UP;
            trunk(x, y, g - 0.3f, g + 2.2f + s * 0.4f, 0.32f, 5, 0x7A5230u);
            blob(x, y, g + 1.8f + s * 1.25f, s * 1.25f, s * 1.15f, col);
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
            /* MEDIUM and HIGH: a pine, two smooth tiers on a stub of trunk. */
            variant = WG_MEDIUM_UP;
            trunk(x, y, g - 0.3f, g + 1.2f, 0.3f, 5, 0x6A4628u);
            cone(x, y, g + 0.6f, g + 0.6f + (top - g) * 0.62f, s * 1.25f, 7, mb_shade(col, 0.92f));
            cone(x, y, g + 0.6f + (top - g) * 0.36f, top + 0.4f, s * 0.95f, 7, col);
        }
        variant = WG_SHARED;
        placed++;
    }
    printf("fwgen: %d trees\n", placed);
}

/* ---- the road network, the tour ------------------------------------------------ */

/* Each depot's gate faces the nearest town, where its road will go. */
static void face_depots(void)
{
    int i, j, k;
    for (i = 0; i < nplaces; i++) {
        int near = -1;
        float bd = 1e30f, best = -2;
        if (places[i].kind != WG_DEPOT)
            continue;
        for (j = 0; j < ntowns; j++) {
            float d = (places[j].x - places[i].x) * (places[j].x - places[i].x) +
                      (places[j].y - places[i].y) * (places[j].y - places[i].y);
            if (d < bd) {
                bd = d;
                near = j;
            }
        }
        for (k = 0; k < 4 && near >= 0; k++) {
            float h = k * WG_PI / 2, dx = places[near].x - places[i].x, dy = places[near].y - places[i].y;
            float along = (dx * sinf(h) - dy * cosf(h)) / sqrtf(bd);     /* the gate's way: -v */
            if (along > best) {
                best = along;
                places[i].heading = h;
            }
        }
    }
}

/* The roads to build: pairs of nodes. */
#define MAX_CONN 64
static int conn[MAX_CONN][2], nconn;

static int planned(int a, int b)
{
    int i;
    for (i = 0; i < nconn; i++)
        if ((conn[i][0] == a && conn[i][1] == b) || (conn[i][0] == b && conn[i][1] == a))
            return 1;
    return 0;
}

static void plan(int a, int b)
{
    if (nconn < MAX_CONN && a != b && !planned(a, b)) {
        conn[nconn][0] = a;
        conn[nconn][1] = b;
        nconn++;
    }
}

/* Somewhere a junction may stand: off every place's ground and depot. */
static int junction_ok(float x, float y)
{
    int i;
    if (x < 80 || y < 80 || x > WG_SIZE - 80 || y > WG_SIZE - 80 || in_depot(x, y))
        return 0;
    for (i = 0; i < nplaces; i++) {
        float dx = x - places[i].x, dy = y - places[i].y, r = places[i].r + 60;
        if (dx * dx + dy * dy < r * r)
            return 0;
    }
    return 1;
}

/* Roads that leave a town within TRUNK_ANGLE of one another share their
 * first stretch: a road to a junction out along their mean direction, and
 * from there one to each. They ran side by side before, each at its own
 * height, and cut the ground from under one another. */
#define TRUNK_ANGLE (25.0f * WG_PI / 180.0f)

static void share_trunks(void)
{
    int t;
    for (t = 0; t < ntowns; t++) {
        int mine[MAX_CONN], n = 0, i, j, start = 0, used[MAX_CONN] = { 0 };
        float ang[MAX_CONN], gap = -1;
        for (i = 0; i < nconn; i++)
            if (conn[i][0] == t || conn[i][1] == t) {
                int o = conn[i][0] == t ? conn[i][1] : conn[i][0];
                mine[n] = i;
                ang[n] = atan2f(nodes[o].y - nodes[t].y, nodes[o].x - nodes[t].x);
                n++;
            }
        for (i = 1; i < n; i++)                         /* by angle */
            for (j = i; j > 0 && ang[j - 1] > ang[j]; j--) {
                float fa = ang[j];
                int fm = mine[j];
                ang[j] = ang[j - 1];
                mine[j] = mine[j - 1];
                ang[j - 1] = fa;
                mine[j - 1] = fm;
            }
        for (i = 0; i < n; i++) {                       /* start after the widest gap, so no group wraps round */
            float g = ang[(i + 1) % n] - ang[i] + (i + 1 == n ? 2 * WG_PI : 0);
            if (g > gap) {
                gap = g;
                start = (i + 1) % n;
            }
        }
        for (i = 0; i < n; i++) {
            int first = (start + i) % n, members[MAX_CONN], m = 0, k;
            float sx = 0, sy = 0, near = 1e9f, d, jx, jy;
            if (used[first])
                continue;
            for (k = i; k < n; k++) {
                int q = (start + k) % n;
                float da = ang[q] - ang[first];
                while (da < 0)
                    da += 2 * WG_PI;
                if (da > TRUNK_ANGLE)
                    break;
                members[m++] = q;
            }
            if (m < 2 || nnodes >= WG_MAX_NODES)
                continue;
            for (k = 0; k < m; k++) {
                int c = mine[members[k]], o = conn[c][0] == t ? conn[c][1] : conn[c][0];
                sx += cosf(ang[members[k]]);
                sy += sinf(ang[members[k]]);
                near = DGK_MIN(near, sqrtf(dist2(t, o)));
            }
            d = DGK_MIN(260.0f, near * 0.45f);
            for (k = 0; k < 4; k++, d *= 0.75f) {
                float l = sqrtf(sx * sx + sy * sy);
                jx = nodes[t].x + sx / l * d;
                jy = nodes[t].y + sy / l * d;
                if (junction_ok(jx, jy))
                    break;
            }
            if (k == 4)
                continue;
            nodes[nnodes].x = jx;
            nodes[nnodes].y = jy;
            nodes[nnodes].kind = WG_JUNCTION;
            nodes[nnodes].place = 0xFFFFFFFFu;
            node_h[nnodes] = ground(jx, jy);
            for (k = 0; k < m; k++) {                   /* the town's roads to them now leave from the junction */
                int c = mine[members[k]];
                if (conn[c][0] == t)
                    conn[c][0] = nnodes;
                else
                    conn[c][1] = nnodes;
                used[members[k]] = 1;
            }
            plan(t, nnodes);
            nnodes++;
        }
    }
}

static void build_network(void)
{
    int in_tree[WG_MAX_PLACES] = { 0 }, i, j, k, junctions;
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
    in_tree[0] = 1;                                  /* Prim's minimum spanning tree of the towns */
    for (k = 1; k < ntowns; k++) {
        int ba = -1, bb = -1;
        float bd = 1e30f;
        for (i = 0; i < ntowns; i++)
            for (j = 0; in_tree[i] && j < ntowns; j++)
                if (!in_tree[j] && dist2(i, j) < bd) {
                    bd = dist2(i, j);
                    ba = i;
                    bb = j;
                }
        in_tree[bb] = 1;
        plan(ba, bb);
    }
    for (i = ntowns; i < nplaces; i++) {             /* each depot a dead end off its nearest town */
        int bt = 0;
        for (j = 1; j < ntowns; j++)
            if (dist2(i, j) < dist2(i, bt))
                bt = j;
        plan(bt, i);
    }
    for (i = 0; i < ntowns; i++) {                   /* towns reach their two nearest towns: loops */
        int n1 = -1, n2 = -1;
        for (j = 0; j < ntowns; j++) {
            if (j == i)
                continue;
            if (n1 < 0 || dist2(i, j) < dist2(i, n1)) {
                n2 = n1;
                n1 = j;
            } else if (n2 < 0 || dist2(i, j) < dist2(i, n2))
                n2 = j;
        }
        if (n1 >= 0)
            plan(i, n1);
        if (n2 >= 0)
            plan(i, n2);
    }
    share_trunks();
    junctions = nnodes - nplaces;
    for (i = 0; i < nconn; i++)
        add_road(conn[i][0], conn[i][1]);
    printf("fwgen: %d roads, %d junctions where roads leaving a town share a trunk\n", nedges, junctions);
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
            /* Over a metre longer: the same cycle found from another town, its length summed in another order,
             * must not win by rounding. */
            if (metres + edges[e].length > best_metres + 1.0f) {
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
    int start, used_node[WG_MAX_NODES], used_edge[MAX_EDGES], i, k;
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
    {
        /* It starts at its town nearest the middle of the map, where the rig starts (WG_SPAWN), whatever town
         * the search began at. */
        int first = 0, rot[64] = { 0 }, rot_e[64] = { 0 };
        float bd = 1e30f;
        for (i = 0; i < best_len; i++) {
            float dx = nodes[best_cycle[i]].x - WG_SIZE / 2, dy = nodes[best_cycle[i]].y - WG_SIZE / 2;
            if (nodes[best_cycle[i]].kind == WG_TOWN && dx * dx + dy * dy < bd) {
                bd = dx * dx + dy * dy;
                first = i;
            }
        }
        for (i = 0; i < best_len; i++) {
            rot[i] = best_cycle[(first + i) % best_len];
            rot_e[i] = cycle_edges[(first + i) % best_len];
        }
        memcpy(best_cycle, rot, sizeof(int) * (size_t)best_len);
        memcpy(cycle_edges, rot_e, sizeof(int) * (size_t)best_len);
    }
    for (i = 0; i < best_len; i++) {                 /* the cycle's roads, each the right way round */
        const wg_edge *e = &edges[cycle_edges[i]], *next = &edges[cycle_edges[(i + 1) % best_len]];
        int forward = (int)e->a == best_cycle[i], at = best_cycle[(i + 1) % best_len], ring = nodes[at].kind == WG_TOWN;
        /* Into a town, the road's last point too (on the ring), then round to where the next road leaves it. */
        for (k = 0; k + (ring ? 0 : 1) < (int)e->npoints && ntour < MAX_POINTS; k++) {
            int p = (int)e->first_point + (forward ? k : (int)e->npoints - 1 - k);
            tour[ntour][0] = pts[p][0];
            tour[ntour][1] = pts[p][1];
            ntour++;
        }
        if (ring) {
            int q = (int)next->a == at ? (int)next->first_point : (int)(next->first_point + next->npoints - 1);
            ntour += wg_ring_arc(nodes[at].x, nodes[at].y, tour[ntour - 1][0], tour[ntour - 1][1], pts[q][0], pts[q][1],
                                 &tour[ntour], MAX_POINTS - ntour);
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
    wg_variant *var = (wg_variant *)calloc(WG_CHUNKS * WG_CHUNKS, sizeof *var);
    size_t run_tris[WG_VARIANTS] = { 0, 0, 0, 0 };
    for (c = 0; c < WG_CHUNKS * WG_CHUNKS; c++) {
        /* The chunk's three runs one after another (LOW's, the shared, MEDIUM and HIGH's), each's indices
         * moved past the vertices before it. */
        float ox = (c % WG_CHUNKS) * WG_CHUNK, oy = (c / WG_CHUNKS) * WG_CHUNK, hmin = 1e9f, hmax = -1e9f;
        int r, base = 0;
        index[c].first_vertex = (uint32_t)nv;
        index[c].first_index = (uint32_t)nt * 3;
        for (r = 0; r < WG_VARIANTS; r++) {
            /* The run's vertices as the pack keeps them (16-bit, chunk-local), each kept once: two the same
             * (a corner of the road's quads, of the cut terrain's pieces) are one, which DOS-GL transforms once. */
            cmesh *m = &chunks[r][c];
            int *remap = (int *)malloc(sizeof(int) * (size_t)(m->nverts + 1)), hcap = 1, kept = 0, *slot;
            while (hcap < m->nverts * 2 + 2)
                hcap *= 2;
            slot = (int *)malloc(sizeof(int) * (size_t)hcap);
            for (i = 0; i < hcap; i++)
                slot[i] = -1;
            verts = (wg_vertex *)grow(verts, &cap_v, (nv + (size_t)m->nverts) * sizeof *verts);
            tris = (uint16_t *)grow(tris, &cap_t, (nt + (size_t)m->ntris) * 6);
            for (i = 0; i < m->nverts; i++) {
                wg_vertex q;
                uint32_t hsh;
                float x = m->pos[i * 3], h = m->pos[i * 3 + 1], y = -m->pos[i * 3 + 2];
                int at;
                q.x = (int16_t)lrintf((x - ox) * WG_UNIT);
                q.y = (int16_t)lrintf(h * WG_UNIT);
                q.z = (int16_t)lrintf((oy - y) * WG_UNIT);
                q.pad = 0;
                memcpy(q.rgba, &m->rgba[i * 4], 4);
                hsh = dgk_crc32(0, &q, sizeof q) & (uint32_t)(hcap - 1);
                for (at = (int)hsh; slot[at] >= 0 && memcmp(&verts[nv + (size_t)slot[at]], &q, sizeof q); at = (at + 1) & (hcap - 1))
                    continue;
                if (slot[at] < 0) {
                    slot[at] = kept;
                    verts[nv + (size_t)kept++] = q;
                }
                remap[i] = slot[at];
                hmin = DGK_MIN(hmin, h);
                hmax = DGK_MAX(hmax, h);
            }
            for (i = 0; i < m->ntris * 3; i++)
                tris[nt * 3 + (size_t)i] = (uint16_t)(remap[m->idx[i]] + base);
            if (base + kept > 65535) {
                fprintf(stderr, "fwgen: chunk %d has %d vertices, more than 16-bit indices reach\n", c, base + kept);
                exit(1);
            }
            free(remap);
            free(slot);
            base += kept;
            run_tris[r] += (size_t)m->ntris;
            nv += (size_t)kept;
            nt += (size_t)m->ntris;
            index[c].ntris += (uint32_t)m->ntris;
        }
        index[c].nverts = (uint32_t)base;
        var[c].low_tris = (uint32_t)chunks[WG_LOW_ONLY][c].ntris;
        var[c].medium_tris = (uint32_t)chunks[WG_MEDIUM_UP][c].ntris;
        var[c].high_tris = (uint32_t)chunks[WG_HIGH_ONLY][c].ntris;
        index[c].hmin = (int16_t)floorf((base ? hmin : 0) * 10);
        index[c].hmax = (int16_t)ceilf((base ? hmax : 0) * 10);
    }
    {
        wg_world h;
        memset(&h, 0, sizeof h);
        h.version = WG_VERSION; h.seed = seed_g; h.chunks = WG_CHUNKS; h.hn = WG_HN;
        h.size = WG_SIZE; h.chunk = WG_CHUNK; h.cell = WG_CELL; h.unit = WG_UNIT;
        pakw_section(w, WG_WORLD, &h, sizeof h);
    }
    pakw_section(w, WG_CINDEX, index, sizeof *index * WG_CHUNKS * WG_CHUNKS);
    pakw_section(w, WG_CVERTS, verts, (uint32_t)(nv * sizeof *verts));
    main_verts = (uint32_t)nv;
    main_tris = (uint32_t)nt;
    pakw_section(w, WG_CTRIS, tris, (uint32_t)(nt * 6));
    pakw_section(w, WG_CVARIANT, var, sizeof *var * WG_CHUNKS * WG_CHUNKS);
    free(var);
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
    printf("fwgen: %lu vertices, %lu triangles in %d chunks (LOW only %lu, shared %lu, MEDIUM and HIGH %lu, HIGH only "
           "%lu), %d boxes, %d roads (%d points), %d depots\n", (unsigned long)nv, (unsigned long)nt,
           WG_CHUNKS * WG_CHUNKS, (unsigned long)run_tris[WG_LOW_ONLY], (unsigned long)run_tris[WG_SHARED],
           (unsigned long)run_tris[WG_MEDIUM_UP], (unsigned long)run_tris[WG_HIGH_ONLY], nboxes, nedges, npts, ndepots);
    free(index);
    free(verts);
    free(tris);
    return pakw_write(w, path);
}

/* WORLDHI.PAK: each chunk's HIGH triangles (the shared, MEDIUM-and-HIGH and HIGH-only runs, as HIGH draws them) with
 * their places in the atlas, welded on place, colour and place in the atlas (wgen.h). */
static int write_hi(const char *path)
{
    pakw *w = pakw_new(seed_g, 0);
    wg_chunk *index = (wg_chunk *)calloc(WG_CHUNKS * WG_CHUNKS, sizeof *index);
    wg_hvertex *verts = NULL;
    uint16_t *tris = NULL;
    size_t cap_v = 0, cap_t = 0, nv = 0, nt = 0;
    wg_hi h;
    int c, i;
    for (c = 0; c < WG_CHUNKS * WG_CHUNKS; c++) {
        float ox = (c % WG_CHUNKS) * WG_CHUNK, oy = (c / WG_CHUNKS) * WG_CHUNK;
        int r, base = 0, total = 0, hcap = 1, *slot;
        for (r = WG_SHARED; r < WG_VARIANTS; r++)
            total += chunks[r][c].nverts;
        while (hcap < total * 2 + 2)
            hcap *= 2;
        slot = (int *)malloc(sizeof(int) * (size_t)hcap);
        for (i = 0; i < hcap; i++)
            slot[i] = -1;
        index[c].first_vertex = (uint32_t)nv;
        index[c].first_index = (uint32_t)nt * 3;
        verts = (wg_hvertex *)grow(verts, &cap_v, (nv + (size_t)total) * sizeof *verts);
        for (r = WG_SHARED; r < WG_VARIANTS; r++) {
            cmesh *m = &chunks[r][c];
            int *remap = (int *)malloc(sizeof(int) * (size_t)(m->nverts + 1));
            tris = (uint16_t *)grow(tris, &cap_t, (nt + (size_t)m->ntris) * 6);
            for (i = 0; i < m->nverts; i++) {
                wg_hvertex q;
                float x = m->pos[i * 3], hh = m->pos[i * 3 + 1], y = -m->pos[i * 3 + 2];
                uint32_t hsh;
                int at;
                memset(&q, 0, sizeof q);
                q.x = (int16_t)lrintf((x - ox) * WG_UNIT);
                q.y = (int16_t)lrintf(hh * WG_UNIT);
                q.z = (int16_t)lrintf((oy - y) * WG_UNIT);
                q.s = (int16_t)lrintf(m->uv[i * 2] * WG_ATLAS);
                q.t = (int16_t)lrintf(m->uv[i * 2 + 1] * WG_ATLAS);
                memcpy(q.rgba, &m->rgba[i * 4], 4);
                hsh = dgk_crc32(0, &q, sizeof q) & (uint32_t)(hcap - 1);
                for (at = (int)hsh; slot[at] >= 0 && memcmp(&verts[nv + (size_t)slot[at]], &q, sizeof q); at = (at + 1) & (hcap - 1))
                    continue;
                if (slot[at] < 0) {
                    slot[at] = base;
                    verts[nv + (size_t)base++] = q;
                }
                remap[i] = slot[at];
            }
            for (i = 0; i < m->ntris * 3; i++)
                tris[nt * 3 + (size_t)i] = (uint16_t)remap[m->idx[i]];
            nt += (size_t)m->ntris;
            index[c].ntris += (uint32_t)m->ntris;
            free(remap);
        }
        if (base > 65535) {
            fprintf(stderr, "fwgen: chunk %d's HIGH has %d vertices, more than 16-bit indices reach\n", c, base);
            exit(1);
        }
        index[c].nverts = (uint32_t)base;
        nv += (size_t)base;
        free(slot);
    }
    memset(&h, 0, sizeof h);
    h.version = WG_HI_VERSION;
    h.seed = seed_g;
    h.main_verts = main_verts;
    h.main_tris = main_tris;
    pakw_section(w, WG_HI_HEADER, &h, sizeof h);
    pakw_section(w, WG_HI_INDEX, index, (uint32_t)(WG_CHUNKS * WG_CHUNKS * sizeof *index));
    pakw_section(w, WG_HI_VERTS, verts, (uint32_t)(nv * sizeof *verts));
    pakw_section(w, WG_HI_TRIS, tris, (uint32_t)(nt * 6));
    printf("fwgen: HIGH: %lu vertices, %lu triangles\n", (unsigned long)nv, (unsigned long)nt);
    free(index);
    free(verts);
    free(tris);
    return pakw_write(w, path);
}

/* A pack's size and hash (the CRC-32 of its bytes), printed: data-check compares the hash with a golden. */
static void report(const char *path)
{
    FILE *f = fopen(path, "rb");
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
        printf("fwgen: %s %ld bytes, hash %08lx\n", path, total, (unsigned long)crc);
    }
}

int main(int argc, char **argv)
{
    static wg_depot depots[WG_MAX_PLACES];
    int i, e;
    FILE *f;
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: fwgen SEED OUT.PAK [HIGH.PAK]\n");
        return 2;
    }
    seed_g = (uint32_t)strtoul(argv[1], NULL, 0);
    dgk_rng_seed(&rng, seed_g, 7);
    make_hills();
    place_kind(WG_TOWN, 5, 950.0f, 450.0f, 170.0f, 250.0f);
    ntowns = nplaces;
    place_kind(WG_DEPOT, 8, 560.0f, 320.0f, 60.0f, 60.0f);
    ndepots = nplaces - ntowns;
    face_depots();
    for (i = 0; i < nplaces; i++) {                  /* flat ground for towns and depots */
        places[i].h = ground(places[i].x, places[i].y);
        if (places[i].kind == WG_TOWN)
            level_town(&places[i]);
        else
            level_depot(&places[i]);
    }
    apply_levelling();
    memset(road_weight, 0, sizeof road_weight);
    build_network();
    if (crossing_roads) {
        fprintf(stderr, "fwgen: %d roads cross a depot\n", crossing_roads);
        return 1;
    }
    for (e = 0; e < nedges; e++)                     /* the roads, levelled into the hills */
        for (i = (int)edges[e].first_point; i + 1 < (int)(edges[e].first_point + edges[e].npoints); i++)
            level_segment(pts[i][0], pts[i][1], pts[i][2], pts[i + 1][0], pts[i + 1][1], pts[i + 1][2], 12.0f, 28.0f);
    apply_levelling();
    fit_surfaces();
    build_terrain();
    for (e = 0; e < nedges; e++) {
        /* Painted, but not the last few metres to a roundabout's ring or a junction, where roads meet. */
        paint pt;
        pt.dashes = pt.edges = 1;
        pt.quiet0 = nodes[edges[e].a].kind == WG_TOWN ? 6.0f : nodes[edges[e].a].kind == WG_JUNCTION ? 14.0f : 0.0f;
        pt.quiet1 = nodes[edges[e].b].kind == WG_TOWN ? 6.0f : nodes[edges[e].b].kind == WG_JUNCTION ? 14.0f : 0.0f;
        road_ribbon((const float (*)[3])&pts[edges[e].first_point], (int)edges[e].npoints, 4.5f, &pt,
                    0.16f + 0.02f * e);                     /* each road its own height: no z-fighting where they meet */
    }
    for (i = 0; i < nplaces; i++)
        if (places[i].kind == WG_TOWN)
            build_town(&places[i]);
    for (i = ntowns; i < nplaces; i++) {
        float s[2][3];
        build_depot(&places[i], &depots[i - ntowns], i - ntowns + 1);
        depots[i - ntowns].node = (uint32_t)i;
        depot_frame(&places[i], 0, -52, &s[0][0], &s[0][1]);        /* the gate to the apron */
        depot_frame(&places[i], 0, -34, &s[1][0], &s[1][1]);
        s[0][2] = s[1][2] = places[i].h;
        road_ribbon((const float (*)[3])s, 2, 4.5f, NULL, 0.14f);
    }
    build_hedges();
    build_roadside();
    build_trees();
    add_box(WG_SIZE / 2, -1, WG_SIZE / 2, 1, 0);          /* the world's edge */
    add_box(WG_SIZE / 2, WG_SIZE + 1, WG_SIZE / 2, 1, 0);
    add_box(-1, WG_SIZE / 2, WG_SIZE / 2, 1, WG_PI / 2);
    add_box(WG_SIZE + 1, WG_SIZE / 2, WG_SIZE / 2, 1, WG_PI / 2);
    build_tour();
    if (write_pack(argv[2], depots) != 0)
        return 1;
    report(argv[2]);
    if (argc == 4) {                                 /* HIGH's textured world beside it */
        if (write_hi(argv[3]) != 0)
            return 1;
        report(argv[3]);
    }
    (void)f;
    return 0;
}
