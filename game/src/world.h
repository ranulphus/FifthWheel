/* world.h - the loaded world: what to draw, 2D collision boxes, the ground's
 * height, the road tour the autopilot follows, the depots, the start.
 * Ground-plane coordinates: x east, y north, metres (GL: x, height, -y).
 *
 * Two kinds of pack: the generated world (wgen.h: 128 m chunks, a
 * heightfield, the road graph, depots) and F1's small test yard (one mesh:
 * FWMS, flat ground). Both have FWCO boxes, an FWPA path and FWSP. */
#ifndef FW_WORLD_H
#define FW_WORLD_H

#include "dgk/pak.h"
#include "dgk/gfx.h"
#include "wgen.h"

typedef struct obb {
    float cx, cy, hl, hw, angle;
} obb;

#define WORLD_GRID 64.0f                 /* collision box buckets */
#define WORLD_NEAR_MAX 128
#define WORLD_PROPS 16                   /* boxes that come and go (parked trailers) */

typedef struct world {
    dgk_pak pak;
    int chunked;
    dgk_mesh mesh;                       /* the yard */
    const wg_world *hdr;                 /* the generated world */
    const wg_chunk *chunks;
    const wg_vertex *verts;
    const uint16_t *tris;
    const int16_t *height;
    const wg_depot *depots;
    int ndepots;
    const obb *boxes;
    int nboxes;
    int grid_n;                          /* buckets per side */
    int *bucket_first, *bucket_items;    /* box indices by bucket */
    obb props[WORLD_PROPS];              /* set by the game; collide like the pack's boxes */
    int nprops;
    const float *path;
    int npath;
    float spawn_x, spawn_y, spawn_heading;
    int drawn_chunks, culled_chunks;     /* last frame: drawn, and in reach but out of view */
} world;

#define FW_FOURCC_MESH DGK_FOURCC('F', 'W', 'M', 'S')
#define FW_FOURCC_COLL WG_BOXES
#define FW_FOURCC_PATH WG_TOUR
#define FW_FOURCC_SPAWN WG_SPAWN

int  world_load(world *w, const char *path);
void world_free(world *w);
float world_height(const world *w, float x, float y);
/* Boxes whose buckets lie within r metres of (x, y), and props that near;
 * up to max. */
int  world_boxes_near(const world *w, float x, float y, float r, const obb **out, int max);
/* The road graph of a generated world (NULL for the yard): header, nodes,
 * edges, points (x, y pairs). */
const wg_graph *world_graph(const world *w, const wg_node **n, const wg_edge **e, const float **p);
/* Draw what can be seen around the camera's target (x, y), radius r: the
 * chunks in reach whose boxes (with their height range) meet the view. */
void world_draw(world *w, float x, float y, float r);

/* Separating axes: 0 when a and b do not overlap; otherwise 1, with the
 * shortest push (mx, my) that moves a out of b. */
int obb_overlap(const obb *a, const obb *b, float *mx, float *my);

#endif
