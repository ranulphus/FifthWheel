/* wgen.h - the generated world's constants and pack layout, shared by the
 * generator (tools/fwgen.c) and the game (world.c). docs/formats.md has
 * the tables. Ground-plane coordinates: x east, y north, metres, the world
 * from (0,0) to (WG_SIZE, WG_SIZE); GL is (x, height, -y). */
#ifndef FW_WGEN_H
#define FW_WGEN_H

#include "dgk/base.h"
#include "dgk/pak.h"
#include <math.h>

#define WG_PI 3.14159265f
#define WG_SIZE 4096.0f
#define WG_CHUNK 128.0f                 /* metres */
#define WG_CHUNKS 32                    /* per side */
#define WG_CELL 16.0f                   /* heightfield spacing */
#define WG_HN 257                       /* heightfield samples per side */
#define WG_UNIT 32.0f                   /* chunk-local vertex units per metre */
#define WG_MAX_PLACES 24
#define WG_MAX_NODES 64                 /* places and the road graph's junctions */

enum { WG_TOWN, WG_DEPOT, WG_JUNCTION };   /* a node's kind; a junction has no place */

/* A roundabout at each town's middle: its ring's centre line WG_RING_R
 * from the town's node, where the town's roads stop; inside it an island,
 * solid to its kerb at WG_RING_ISLAND. Traffic goes round clockwise. */
#define WG_RING_R 24.0f
#define WG_RING_ISLAND 18.0f

/* Sections. */
#define WG_WORLD  DGK_FOURCC('W', 'R', 'L', 'D')   /* wg_world */
#define WG_CINDEX DGK_FOURCC('C', 'I', 'D', 'X')   /* wg_chunk[WG_CHUNKS * WG_CHUNKS], row-major from the south-west */
#define WG_CVERTS DGK_FOURCC('C', 'V', 'R', 'T')   /* wg_vertex[] */
#define WG_CTRIS  DGK_FOURCC('C', 'T', 'R', 'I')   /* u16[] indices, chunk-local */
#define WG_HEIGHT DGK_FOURCC('H', 'G', 'H', 'T')   /* int16[WG_HN * WG_HN], decimetres */
#define WG_BOXES  DGK_FOURCC('F', 'W', 'C', 'O')   /* u32 n, then obb[n] (world.h) */
#define WG_GRAPH  DGK_FOURCC('G', 'R', 'P', 'H')   /* wg_graph, nodes, edges, points */
#define WG_DEPOTS DGK_FOURCC('F', 'W', 'D', 'P')   /* u32 n, then wg_depot[n] */
#define WG_TOUR   DGK_FOURCC('F', 'W', 'P', 'A')   /* u32 n, then n points {float x, y}: the autopilot's tour */
#define WG_SPAWN  DGK_FOURCC('F', 'W', 'S', 'P')   /* float x, y, heading */
#define WG_CVARIANT DGK_FOURCC('C', 'V', 'A', 'R') /* wg_variant[WG_CHUNKS * WG_CHUNKS]: what each detail draws */

typedef struct wg_world {
    uint32_t version, seed, chunks, hn;
    float size, chunk, cell, unit;
} wg_world;

typedef struct wg_chunk {
    uint32_t first_vertex, nverts;      /* into CVRT */
    uint32_t first_index, ntris;        /* into CTRI */
    int16_t hmin, hmax;                 /* decimetres: the chunk's height range, for culling */
} wg_chunk;

/* A chunk's triangles in four runs, one after another: low_tris drawn only
 * at LOW (detail.h's models 0: the plainer world), then those every detail
 * draws, then medium_tris at MEDIUM and HIGH (models 1 and 2), then
 * high_tris at HIGH alone (models 2). Each detail draws one stretch of them.
 * Without the section, all. */
typedef struct wg_variant {
    uint32_t low_tris, medium_tris, high_tris;
} wg_variant;

enum { WG_LOW_ONLY, WG_SHARED, WG_MEDIUM_UP, WG_HIGH_ONLY, WG_VARIANTS };   /* the runs, as fwgen builds them */

typedef struct wg_vertex {
    int16_t x, y, z, pad;               /* chunk-local GL coordinates, WG_UNIT per metre */
    uint8_t rgba[4];
} wg_vertex;

typedef struct wg_graph {
    uint32_t nnodes, nedges, npoints;
} wg_graph;

typedef struct wg_node {
    float x, y;
    uint32_t kind, place;               /* WG_TOWN / WG_DEPOT, its index */
} wg_node;

typedef struct wg_edge {
    uint32_t a, b, first_point, npoints;
    float length;
} wg_edge;

#define WG_BAYS 3
typedef struct wg_depot {
    float x, y, heading;                /* apron centre; heading: the dock faces the opposite way */
    float bay[WG_BAYS][3];              /* where a docked trailer's rear axle stops: x, y, heading */
    float pickup[3];                    /* where a trailer waits to be coupled */
    uint32_t node;                      /* its node in the road graph */
} wg_depot;

/* The points (at most max) strictly between two on a town's ring (its
 * middle cx, cy), going round clockwise from (x0, y0) to (x1, y1), about
 * every 6 m: a route through the roundabout. Returns how many. */
static inline int wg_ring_arc(float cx, float cy, float x0, float y0, float x1, float y1, float (*out)[2], int max)
{
    float a0 = atan2f(y0 - cy, x0 - cx), a1 = atan2f(y1 - cy, x1 - cx), sweep = a0 - a1;
    int m, k, n = 0;
    while (sweep <= 0.0f)
        sweep += 2 * WG_PI;
    while (sweep > 2 * WG_PI)
        sweep -= 2 * WG_PI;
    m = (int)ceilf(sweep * WG_RING_R / 6.0f);
    for (k = 1; k < m && n < max; k++, n++) {
        float a = a0 - sweep * (float)k / (float)m;
        out[n][0] = cx + WG_RING_R * cosf(a);
        out[n][1] = cy + WG_RING_R * sinf(a);
    }
    return n;
}

#endif
