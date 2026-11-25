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
#define WG_CHUNK 64.0f                  /* metres: culled whole, so small (version 2; 128 m in 1) */
#define WG_CHUNKS 64                    /* per side */
#define WG_VERSION 2
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
#define WG_TOWNS  DGK_FOURCC('F', 'W', 'T', 'N')   /* u32 n, then wg_town[n] */
#define WG_STOPS  DGK_FOURCC('F', 'W', 'S', 'T')   /* u32 n, then wg_stop[n]: by town */

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

/* A town's streets: a square grid in its own frame (u along its heading, v
 * to the left of it), every WG_GRID metres from -blocks to +blocks each way;
 * its roundabout in the middle, where the two middle streets stop at the
 * ring. A point (u, v) is at x = x + u cos(heading) - v sin(heading),
 * y = y + u sin(heading) + v cos(heading). */
#define WG_GRID 64.0f
typedef struct wg_town {
    float x, y, heading, r;             /* r: the blocks with buildings are those whose middles are within it */
    uint32_t blocks, node;              /* streets from -blocks to +blocks (times WG_GRID); its node in the graph */
    uint32_t first_stop, nstops;        /* its parcel stops in FWST */
} wg_town;

/* A parcel stop: in a street's left lane before a house's or shop's door,
 * heading along the lane (the door on the left). */
enum { WG_STOP_HOUSE, WG_STOP_SHOP };
typedef struct wg_stop {
    float x, y, heading;
    uint16_t town, kind;
} wg_stop;

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

/* Traffic keeps left: a lane's middle WG_LANE metres left of the road's
 * centre line (on a roundabout, going round clockwise, that is its outer
 * lane). wg_offset moves each of a line's n points `off` metres to its left
 * (to the right if negative), across the mean of the directions either side
 * of it; closed: the line loops. in and out are not the same. */
#define WG_LANE 2.25f
static inline void wg_offset(const float (*in)[2], int n, float off, int closed, float (*out)[2])
{
    int i;
    for (i = 0; i < n; i++) {
        int a = i > 0 ? i - 1 : closed ? n - 1 : 0, b = i + 1 < n ? i + 1 : closed ? 0 : n - 1;
        float dx = in[b][0] - in[a][0], dy = in[b][1] - in[a][1], l = sqrtf(dx * dx + dy * dy);
        out[i][0] = in[i][0];
        out[i][1] = in[i][1];
        if (l > 1e-4f) {
            out[i][0] -= dy / l * off;
            out[i][1] += dx / l * off;
        }
    }
}

/* HIGH's textured world, WORLDHI.PAK beside WORLD.PAK (fwgen writes both;
 * the game loads it only for HIGH): each chunk's HIGH triangles again (the
 * shared, MEDIUM-and-HIGH and HIGH-only runs as one), each vertex with its
 * place in the atlas (game/src/tex.c). The atlas is WG_ATLAS texels square,
 * tiles of WG_TILE in rows, each face mapping one tile inside a WG_GUTTER
 * margin (no repeat: the filtering stays in it); greyscale cartoon patterns
 * tinted by the vertex colours. */
#define WG_HI_HEADER DGK_FOURCC('H', 'W', 'L', 'D')  /* wg_hi */
#define WG_HI_INDEX  DGK_FOURCC('H', 'I', 'D', 'X')  /* wg_chunk[WG_CHUNKS * WG_CHUNKS] (hmin, hmax unused) */
#define WG_HI_VERTS  DGK_FOURCC('H', 'V', 'R', 'T')  /* wg_hvertex[] */
#define WG_HI_TRIS   DGK_FOURCC('H', 'T', 'R', 'I')  /* uint16[3] a triangle, chunk-local */
#define WG_HI_VERSION 1

typedef struct wg_hi {
    uint32_t version, seed, main_verts, main_tris;   /* the WORLD.PAK made with it: its seed, its CVRT's and CTRI's counts */
} wg_hi;

typedef struct wg_hvertex {
    int16_t x, y, z;                    /* as wg_vertex */
    int16_t s, t;                       /* the atlas's texels (the texture matrix scales them by 1 / WG_ATLAS) */
    uint8_t rgba[4];
    int16_t pad;
} wg_hvertex;

#define WG_ATLAS 512
#define WG_TILE 128
#define WG_GUTTER 4
enum { WG_TX_PLAIN, WG_TX_GRASS, WG_TX_CROPS, WG_TX_CROPS_45, WG_TX_CROPS_90, WG_TX_CROPS_135, WG_TX_ASPHALT,
       WG_TX_BRICK, WG_TX_BOARDS, WG_TX_ROOF, WG_TX_LEAVES, WG_TX_PAVING, WG_TX_COUNT };
static inline void wg_tile_uv(int tile, float u, float v, float *s, float *t)
{
    const int per = WG_ATLAS / WG_TILE;
    float inner = (float)(WG_TILE - 2 * WG_GUTTER);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    *s = ((float)((tile % per) * WG_TILE + WG_GUTTER) + u * inner) / WG_ATLAS;
    *t = ((float)((tile / per) * WG_TILE + WG_GUTTER) + v * inner) / WG_ATLAS;
}

#endif
