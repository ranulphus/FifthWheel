/* mesh.h - building vertex-coloured meshes with baked light (a warm sun from
 * the upper left, strong ambient), in GL coordinates: x east, y up, z south
 * (the game's ground plane is (x, -z)). Used by the game and by the host
 * tools that make its packs. */
#ifndef FW_MESH_H
#define FW_MESH_H

#include "dgk/gfx.h"

typedef struct builder {
    float *pos;
    uint8_t *rgba;
    uint16_t *idx;
    int nverts, ntris, max_verts, max_tris, overflow;
} builder;

void mb_init(builder *b, float *pos, uint8_t *rgba, uint16_t *idx, int max_verts, int max_tris);
void mb_quad(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb);
void mb_quad_lit(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb,
                 float light);        /* a given light factor instead of the sun's */
void mb_box(builder *b, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb);
void mb_mesh(dgk_mesh *m, const builder *b);
uint32_t mb_shade(uint32_t rgb, float k);

#endif
