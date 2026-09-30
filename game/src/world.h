/* world.h - the loaded world: meshes to draw, 2D collision boxes, the road
 * path the autopilot follows, the start. Ground-plane coordinates: x east,
 * y north, metres (GL: x, height, -y). Pack sections (docs/formats.md):
 *   FWMS  u32 nverts, u32 ntris, float pos[3n], u8 rgba[4n], u16 idx[3t]
 *   FWCO  u32 n, then n boxes {float cx, cy, half_len, half_wid, angle}
 *   FWPA  u32 n, then n points {float x, y} (a closed loop)
 *   FWSP  float x, y, heading                                          */
#ifndef FW_WORLD_H
#define FW_WORLD_H

#include "dgk/pak.h"
#include "dgk/gfx.h"

typedef struct obb {
    float cx, cy, hl, hw, angle;
} obb;

typedef struct world {
    dgk_pak pak;
    dgk_mesh mesh;
    const obb *boxes;
    int nboxes;
    const float *path;
    int npath;
    float spawn_x, spawn_y, spawn_heading;
} world;

#define FW_FOURCC_MESH DGK_FOURCC('F', 'W', 'M', 'S')
#define FW_FOURCC_COLL DGK_FOURCC('F', 'W', 'C', 'O')
#define FW_FOURCC_PATH DGK_FOURCC('F', 'W', 'P', 'A')
#define FW_FOURCC_SPAWN DGK_FOURCC('F', 'W', 'S', 'P')

int  world_load(world *w, const char *path);
void world_free(world *w);

/* Separating axes: 0 when a and b do not overlap; otherwise 1, with the
 * shortest push (mx, my) that moves a out of b. */
int obb_overlap(const obb *a, const obb *b, float *mx, float *my);

#endif
