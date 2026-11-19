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
    float *nrm;                /* optional (mb_normals): each vertex's unit normal... */
    uint32_t *base;            /* ...and its unlit colour, to light it again (mb_relight) */
    int nverts, ntris, max_verts, max_tris, overflow;
} builder;

void mb_init(builder *b, float *pos, uint8_t *rgba, uint16_t *idx, int max_verts, int max_tris);
void mb_quad(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb);
void mb_quad_lit(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb,
                 float light);        /* a given light factor instead of the sun's */
void mb_box(builder *b, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb);
void mb_mesh(dgk_mesh *m, const builder *b);
uint32_t mb_shade(uint32_t rgb, float k);

/* Smooth shapes. With mb_normals, every vertex also keeps its normal and its
 * unlit colour, so the mesh can be lit again as it turns (mb_relight). */
void mb_normals(builder *b, float *nrm, uint32_t *base);
/* A vertex (position, unit normal, unlit colour), lit by the sun: its index. */
int  mb_vertex(builder *b, const float *p, const float *n, uint32_t rgb);
void mb_tri(builder *b, int v0, int v1, int v2);
/* A box with its edges and corners rounded to radius r, in k steps a quarter
 * turn, smooth-shaded (k 0 or r 0: mb_box, square and flat). */
void mb_rbox(builder *b, float x0, float y0, float z0, float x1, float y1, float z1, float r, int k, uint32_t rgb);
/* A cylinder of n sides round the axis `axis` (0 x, 1 y, 2 z) through c,
 * from c - half to c + half along it: smooth sides in `side`, flat ends in
 * `ends` (which: 1 the + end, 2 the - end, 3 both, 0 none). Half 0: a disc,
 * its ends only. */
void mb_cylinder(builder *b, int axis, const float *c, float radius, float half, int n, uint32_t side, uint32_t ends,
                 int which);
/* Light a mesh made with mb_normals again, turned `yaw` radians about the
 * vertical (as glRotatef(yaw, 0, 1, 0) turns it): the sun stays put in the
 * world while the mesh turns under it. */
void mb_relight(const builder *b, float yaw);
/* The sun's light on a surface with unit normal (nx, ny, nz), GL coordinates. */
float mb_light(float nx, float ny, float nz);

#endif
