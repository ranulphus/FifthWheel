/* fwyard - generate the test yard (plan milestone F1): a field, a tarmac
 * yard with shipping containers, an oval road loop for the autopilot and a
 * perimeter wall, written as a pack (game/src/world.h has the sections).
 *
 *   fwyard OUT.PAK
 *
 * Ground-plane coordinates: x east, y north (GL x, height, -y). */
#include "mesh.h"
#include "pakw.h"
#include "../game/src/world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_V 16384
#define MAX_T 16384
#define FW_PI 3.14159265358979f

static float pos[MAX_V * 3];
static uint8_t rgba[MAX_V * 4];
static uint16_t idx[MAX_T * 3];
static builder b;
static obb boxes[64];
static int nboxes;
static float path[512][2];
static int npath;

/* A flat quad on the ground at height h, corners in ground coordinates. */
static void ground_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float h,
                        uint32_t rgb)
{
    float p[4][3] = { { x0, h, -y0 }, { x1, h, -y1 }, { x2, h, -y2 }, { x3, h, -y3 } };
    mb_quad(&b, p[0], p[1], p[2], p[3], rgb);
}

/* A box standing on the ground: centre, half length along angle, half
 * width across, height; with a collision box. */
static void solid(float cx, float cy, float hl, float hw, float angle, float height, uint32_t rgb, int collide)
{
    float c = cosf(angle), s = sinf(angle), corner[4][2];
    float v[8][3];
    int i;
    corner[0][0] = cx - c * hl + s * hw; corner[0][1] = cy - s * hl - c * hw;
    corner[1][0] = cx + c * hl + s * hw; corner[1][1] = cy + s * hl - c * hw;
    corner[2][0] = cx + c * hl - s * hw; corner[2][1] = cy + s * hl + c * hw;
    corner[3][0] = cx - c * hl - s * hw; corner[3][1] = cy - s * hl + c * hw;
    for (i = 0; i < 4; i++) {
        v[i][0] = v[i + 4][0] = corner[i][0];
        v[i][2] = v[i + 4][2] = -corner[i][1];
        v[i][1] = 0;
        v[i + 4][1] = height;
    }
    mb_quad(&b, v[4], v[5], v[6], v[7], rgb);      /* top (the corners run anticlockwise seen from above) */
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        mb_quad(&b, v[i], v[j], v[j + 4], v[i + 4], rgb);
    }
    if (collide && nboxes < (int)(sizeof boxes / sizeof boxes[0])) {
        boxes[nboxes].cx = cx; boxes[nboxes].cy = cy;
        boxes[nboxes].hl = hl; boxes[nboxes].hw = hw; boxes[nboxes].angle = angle;
        nboxes++;
    }
}

/* The oval loop's centre line at distance t (0..length) and its heading. */
#define STRAIGHT 120.0f
#define RADIUS 45.0f
static float loop_length(void)
{
    return 2 * STRAIGHT + 2 * FW_PI * RADIUS;
}

static void loop_at(float t, float *x, float *y, float *heading)
{
    float arc = FW_PI * RADIUS;
    t = fmodf(t, loop_length());
    if (t < STRAIGHT) {                               /* south straight, eastbound */
        *x = -STRAIGHT / 2 + t; *y = -RADIUS; *heading = 0;
    } else if (t < STRAIGHT + arc) {                  /* east bend, turning left */
        float a = (t - STRAIGHT) / RADIUS;
        *x = STRAIGHT / 2 + RADIUS * sinf(a); *y = -RADIUS * cosf(a); *heading = a;
    } else if (t < 2 * STRAIGHT + arc) {              /* north straight, westbound */
        *x = STRAIGHT / 2 - (t - STRAIGHT - arc); *y = RADIUS; *heading = FW_PI;
    } else {                                          /* west bend */
        float a = (t - 2 * STRAIGHT - arc) / RADIUS;
        *x = -STRAIGHT / 2 - RADIUS * sinf(a); *y = RADIUS * cosf(a); *heading = FW_PI + a;
    }
}

static void build(void)
{
    const float half = 140, cell = 40, road = 5.0f;
    float t, len = loop_length();
    int i, j;

    for (j = 0; j < 7; j++)                            /* the field */
        for (i = 0; i < 7; i++) {
            float x0 = -half + i * cell, y0 = -half + j * cell;
            ground_quad(x0, y0, x0 + cell, y0, x0 + cell, y0 + cell, x0, y0 + cell, 0,
                        ((i + j) & 1) ? 0x5DBB3Fu : 0x52AD37u);
        }
    ground_quad(-38, -24, 38, -24, 38, 24, -38, 24, 0.02f, 0x8A8F99u);   /* the yard's tarmac */

    for (t = 0; t < len - 0.01f; t += 5.0f) {         /* the road, in 5 m pieces */
        float x0, y0, h0, x1, y1, h1;
        loop_at(t, &x0, &y0, &h0);
        loop_at(t + 5.0f, &x1, &y1, &h1);
        ground_quad(x0 + sinf(h0) * road, y0 - cosf(h0) * road, x1 + sinf(h1) * road, y1 - cosf(h1) * road,
                    x1 - sinf(h1) * road, y1 + cosf(h1) * road, x0 - sinf(h0) * road, y0 + cosf(h0) * road, 0.03f,
                    0x565B66u);
    }
    for (t = 0; t < len - 0.01f; t += 10.0f) {        /* bold dashes on the centre line */
        float x0, y0, h0, x1, y1, h1, w = 0.25f;
        loop_at(t, &x0, &y0, &h0);
        loop_at(t + 4.0f, &x1, &y1, &h1);
        ground_quad(x0 + sinf(h0) * w, y0 - cosf(h0) * w, x1 + sinf(h1) * w, y1 - cosf(h1) * w,
                    x1 - sinf(h1) * w, y1 + cosf(h1) * w, x0 - sinf(h0) * w, y0 + cosf(h0) * w, 0.05f, 0xFFF4D0u);
    }
    for (t = 0; t < len - 0.01f; t += 4.0f) {         /* the autopilot's path */
        float heading;
        loop_at(t, &path[npath][0], &path[npath][1], &heading);
        npath++;
    }

    /* Shipping containers in the yard: bold colours, 12.2 x 2.44 m. */
    solid(-22, 12, 6.1f, 1.22f, 0, 2.6f, 0xE8402Au, 1);
    solid(-22, 15.5f, 6.1f, 1.22f, 0, 2.6f, 0x2A7FE8u, 1);
    solid(-8, 12, 6.1f, 1.22f, 0, 2.6f, 0x3FC060u, 1);
    solid(18, -10, 6.1f, 1.22f, FW_PI / 2, 2.6f, 0xFF9A2Au, 1);
    solid(22, -10, 6.1f, 1.22f, FW_PI / 2, 2.6f, 0xFFD23Fu, 1);
    solid(5, -14, 6.1f, 1.22f, 0.4f, 2.6f, 0xB050E0u, 1);
    /* A loading dock for reversing practice. */
    solid(30, 16, 8.0f, 3.0f, 0, 1.3f, 0xC8C8D0u, 1);
    /* The perimeter wall. */
    solid(0, -half, half, 0.5f, 0, 1.0f, 0xB8B0A0u, 1);
    solid(0, half, half, 0.5f, 0, 1.0f, 0xB8B0A0u, 1);
    solid(-half, 0, half, 0.5f, FW_PI / 2, 1.0f, 0xB8B0A0u, 1);
    solid(half, 0, half, 0.5f, FW_PI / 2, 1.0f, 0xB8B0A0u, 1);
    /* Lollipop trees outside the loop: trunk and a chunky canopy. */
    {
        static const float trees[][2] = { { -115, -95 }, { -95, 100 }, { 110, 95 }, { 118, -90 },
                                          { 0, 110 }, { 0, -110 }, { -125, 10 }, { 126, -8 } };
        for (i = 0; i < 8; i++) {
            float x = trees[i][0], y = trees[i][1];
            solid(x, y, 0.4f, 0.4f, 0, 3.0f, 0x7A5230u, 1);
            mb_box(&b, x - 2.2f, 2.6f, -y - 2.2f, x + 2.2f, 6.4f, -y + 2.2f, 0x3E9E3Au);
        }
    }
}

int main(int argc, char **argv)
{
    pakw *w;
    uint8_t *mesh;
    uint32_t nv, nt, msize, n;
    float spawn[3];
    if (argc != 2) {
        fprintf(stderr, "usage: fwyard OUT.PAK\n");
        return 2;
    }
    mb_init(&b, pos, rgba, idx, MAX_V, MAX_T);
    build();
    if (b.overflow) {
        fprintf(stderr, "fwyard: mesh too big\n");
        return 1;
    }
    nv = (uint32_t)b.nverts;
    nt = (uint32_t)b.ntris;
    msize = 8 + nv * 16 + nt * 6;
    mesh = (uint8_t *)malloc(msize);
    memcpy(mesh, &nv, 4);
    memcpy(mesh + 4, &nt, 4);
    memcpy(mesh + 8, pos, nv * 12);
    memcpy(mesh + 8 + nv * 12, rgba, nv * 4);
    memcpy(mesh + 8 + nv * 16, idx, nt * 6);
    w = pakw_new(1, 0);
    pakw_section(w, FW_FOURCC_MESH, mesh, msize);
    {
        uint8_t *c = (uint8_t *)malloc(4 + sizeof(obb) * nboxes);
        n = (uint32_t)nboxes;
        memcpy(c, &n, 4);
        memcpy(c + 4, boxes, sizeof(obb) * nboxes);
        pakw_section(w, FW_FOURCC_COLL, c, 4 + (uint32_t)(sizeof(obb) * nboxes));
        free(c);
    }
    {
        uint8_t *p = (uint8_t *)malloc(4 + 8 * npath);
        n = (uint32_t)npath;
        memcpy(p, &n, 4);
        memcpy(p + 4, path, 8 * npath);
        pakw_section(w, FW_FOURCC_PATH, p, 4 + 8u * npath);
        free(p);
    }
    spawn[0] = -40;
    spawn[1] = -RADIUS;
    spawn[2] = 0;
    pakw_section(w, FW_FOURCC_SPAWN, spawn, sizeof spawn);
    free(mesh);
    printf("fwyard: %u vertices, %u triangles, %d boxes, path of %d points\n", nv, nt, nboxes, npath);
    return pakw_write(w, argv[1]) ? 1 : 0;
}
