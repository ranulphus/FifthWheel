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
    uint8_t *rgba;
    uint16_t *idx;
    int nverts, ntris, cap_v, cap_t;
} cmesh;

static cmesh chunks[WG_VARIANTS][WG_CHUNKS * WG_CHUNKS];
static int variant = WG_SHARED;                   /* where quad() puts what it makes */
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

/* A vertex of chunk c's mesh (GL coordinates, an RGB colour): its index. */
static int cmesh_vertex(cmesh *c, const float *p, uint32_t rgb)
{
    if (c->nverts + 1 > c->cap_v) {
        c->cap_v = c->cap_v ? c->cap_v * 2 : 1024;
        c->pos = (float *)realloc(c->pos, (size_t)c->cap_v * 12);
        c->rgba = (uint8_t *)realloc(c->rgba, (size_t)c->cap_v * 4);
    }
    memcpy(&c->pos[c->nverts * 3], p, 3 * sizeof(float));
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
    count = npts - first;
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
        const place *pl = &places[end ? b : a];
        float along = 0, last_on = -1;
        for (k = 0; k < count; k++) {
            int q = end ? count - 1 - k : k, prev = end ? q + 1 : q - 1;
            float w;
            if (k)
                along += sqrtf((pts[first + q][0] - pts[first + prev][0]) * (pts[first + q][0] - pts[first + prev][0]) +
                               (pts[first + q][1] - pts[first + prev][1]) * (pts[first + q][1] - pts[first + prev][1]));
            if (place_flat(pl, pts[first + q][0], pts[first + q][1])) {
                raw[q] = pl->h;
                last_on = along;
                continue;
            }
            if (last_on < 0 || along - last_on >= ROAD_EASE)
                break;
            w = 1.0f - smooth((along - last_on) / ROAD_EASE);
            raw[q] += (pl->h - raw[q]) * w;
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
    float h = t->h;
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
    float x0, y0, x1, y1;
    int k;
    for (k = -2; k <= 2; k++) {
        depot_frame(d, -48, k * 16.0f, &x0, &y0);
        depot_frame(d, 48, k * 16.0f, &x1, &y1);
        level_segment(x0, y0, d->h, x1, y1, d->h, 20.0f, 30.0f);
    }
}

static void build_depot(const place *d, wg_depot *out)
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
    block(x, y, 42, 10, d->heading, 7, 0xE4E0D6u, 0x3A6FD8u, 1, 1);
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

/* A convex piece of a terrain triangle, cut along the regions' boundaries
 * until each piece lies in one region's colour; each a fan of its own
 * vertices. The pieces lie on the triangle (heights interpolated), so the
 * ground is where it was. */
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
        for (i = 0; i < n; i++) {
            float p[3];
            p[0] = v[i].x;
            p[1] = v[i].h;
            p[2] = -v[i].y;
            cmesh_vertex(c, p, mb_shade(k == n ? region_colour(reg[0]) : ground_colour(cx, cy), v[i].light));
        }
        for (i = 1; i + 1 < n; i++)
            cmesh_tri(c, first, first + i, first + i + 1);
        return;
    }
    for (i = 0; i < n; i++)
        f[i] = region_split(reg[0], reg[k], v[i].x, v[i].y);
    for (i = 0; i < n; i++) {                        /* Sutherland-Hodgman: a gets f <= 0, b gets f > 0 */
        int j = (i + 1) % n;
        if (f[i] <= 0)
            a[na++] = v[i];
        else
            b[nb++] = v[i];
        if ((f[i] <= 0) != (f[j] <= 0)) {
            tvert x = f[i] <= 0 ? crossing(v[i], f[i], v[j], f[j]) : crossing(v[j], f[j], v[i], f[i]);
            a[na++] = x;
            b[nb++] = x;
        }
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
#define CHUNK_CELLS 8                    /* WG_CHUNK / WG_CELL: checked in build_terrain */

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
            /* Up to four colours meet at a sample: its vertices so far. */
            static struct { uint32_t rgb; int idx; } seen[CHUNK_CELLS + 1][CHUNK_CELLS + 1][4];
            static int nseen[CHUNK_CELLS + 1][CHUNK_CELLS + 1];
            cmesh *c = &chunks[WG_SHARED][cy * WG_CHUNKS + cx];
            memset(nseen, 0, sizeof nseen);
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
                            terrain_piece(&chunks[WG_HIGH_ONLY][cy * WG_CHUNKS + cx], t1, 3, 0);
                            terrain_piece(&chunks[WG_HIGH_ONLY][cy * WG_CHUNKS + cx], t2, 3, 0);
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
                    rgb = at[0];
                    for (k = 0; k < 4; k++) {
                        int li = i + corner[k][0], lj = j + corner[k][1], s;
                        for (s = 0; s < nseen[lj][li] && seen[lj][li][s].rgb != rgb; s++)
                            continue;
                        if (s == nseen[lj][li]) {
                            float p[3];
                            p[0] = (gi + corner[k][0]) * WG_CELL;
                            p[1] = h_at(gi + corner[k][0], gj + corner[k][1]);
                            p[2] = -(gj + corner[k][1]) * WG_CELL;
                            seen[lj][li][s].rgb = rgb;
                            seen[lj][li][s].idx = cmesh_vertex(c, p, mb_shade(rgb, terrain_light(gi + corner[k][0],
                                                                                                 gj + corner[k][1])));
                            nseen[lj][li]++;
                        }
                        v[k] = seen[lj][li][s].idx;
                    }
                    cmesh_tri(c, v[0], v[1], v[2]);
                    cmesh_tri(c, v[0], v[2], v[3]);
                }
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
        add_road(ba, bb);
    }
    for (i = ntowns; i < nnodes; i++) {              /* each depot a dead end off its nearest town */
        int bt = 0;
        for (j = 1; j < ntowns; j++)
            if (dist2(i, j) < dist2(i, bt))
                bt = j;
        add_road(bt, i);
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
    wg_variant *var = (wg_variant *)calloc(WG_CHUNKS * WG_CHUNKS, sizeof *var);
    size_t run_tris[WG_VARIANTS] = { 0, 0, 0 };
    for (c = 0; c < WG_CHUNKS * WG_CHUNKS; c++) {
        /* The chunk's three runs one after another (LOW's, the shared, MEDIUM and HIGH's), each's indices
         * moved past the vertices before it. */
        float ox = (c % WG_CHUNKS) * WG_CHUNK, oy = (c / WG_CHUNKS) * WG_CHUNK, hmin = 1e9f, hmax = -1e9f;
        int r, base = 0;
        index[c].first_vertex = (uint32_t)nv;
        index[c].first_index = (uint32_t)nt * 3;
        for (r = 0; r < WG_VARIANTS; r++) {
            cmesh *m = &chunks[r][c];
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
            for (i = 0; i < m->ntris * 3; i++)
                tris[nt * 3 + (size_t)i] = (uint16_t)(m->idx[i] + base);
            if (base + m->nverts > 65535) {
                fprintf(stderr, "fwgen: chunk %d has %d vertices, more than 16-bit indices reach\n", c,
                        base + m->nverts);
                exit(1);
            }
            base += m->nverts;
            run_tris[r] += (size_t)m->ntris;
            nv += (size_t)m->nverts;
            nt += (size_t)m->ntris;
            index[c].ntris += (uint32_t)m->ntris;
        }
        index[c].nverts = (uint32_t)base;
        var[c].low_tris = (uint32_t)chunks[WG_LOW_ONLY][c].ntris;
        var[c].high_tris = (uint32_t)chunks[WG_HIGH_ONLY][c].ntris;
        index[c].hmin = (int16_t)floorf((base ? hmin : 0) * 10);
        index[c].hmax = (int16_t)ceilf((base ? hmax : 0) * 10);
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
    printf("fwgen: %lu vertices, %lu triangles in %d chunks (LOW only %lu, shared %lu, MEDIUM and HIGH only %lu), "
           "%d boxes, %d roads (%d points), %d depots\n", (unsigned long)nv, (unsigned long)nt, WG_CHUNKS * WG_CHUNKS,
           (unsigned long)run_tris[WG_LOW_ONLY], (unsigned long)run_tris[WG_SHARED],
           (unsigned long)run_tris[WG_HIGH_ONLY], nboxes, nedges, npts, ndepots);
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
        s[0][2] = s[1][2] = places[i].h;
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
