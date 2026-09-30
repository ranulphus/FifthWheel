/* wgen.h - the generated world's constants and pack layout, shared by the
 * generator (tools/fwgen.c) and the game (world.c). docs/formats.md has
 * the tables. Ground-plane coordinates: x east, y north, metres, the world
 * from (0,0) to (WG_SIZE, WG_SIZE); GL is (x, height, -y). */
#ifndef FW_WGEN_H
#define FW_WGEN_H

#include "dgk/base.h"
#include "dgk/pak.h"

#define WG_PI 3.14159265f
#define WG_SIZE 4096.0f
#define WG_CHUNK 128.0f                 /* metres */
#define WG_CHUNKS 32                    /* per side */
#define WG_CELL 16.0f                   /* heightfield spacing */
#define WG_HN 257                       /* heightfield samples per side */
#define WG_UNIT 32.0f                   /* chunk-local vertex units per metre */
#define WG_MAX_PLACES 24

enum { WG_TOWN, WG_DEPOT };

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

typedef struct wg_world {
    uint32_t version, seed, chunks, hn;
    float size, chunk, cell, unit;
} wg_world;

typedef struct wg_chunk {
    uint32_t first_vertex, nverts;      /* into CVRT */
    uint32_t first_index, ntris;        /* into CTRI */
    int16_t hmin, hmax;                 /* decimetres: the chunk's height range, for culling */
} wg_chunk;

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

#endif
