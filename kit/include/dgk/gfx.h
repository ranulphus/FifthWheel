/* dgk/gfx.h - drawing helpers within DOS-GL's OpenGL 1.1 subset: camera
 * matrices, vertex-coloured meshes from arrays, and the 2D overlay space
 * (640x480 units whatever the resolution). */
#ifndef DGK_GFX_H
#define DGK_GFX_H

#include "dgk/base.h"

typedef struct { float x, y, z; } dgk_v3;

/* A vertex-coloured mesh: positions, RGBA colours, triangle indices. */
typedef struct dgk_mesh {
    const float *pos;          /* x,y,z per vertex */
    const uint8_t *rgba;       /* 4 per vertex */
    const uint16_t *idx;       /* 3 per triangle */
    int nverts, ntris;
} dgk_mesh;

extern uint32_t dgk_gfx_tris;              /* triangles submitted this frame (the loop resets it) */

void dgk_gfx_perspective(float fovy_deg, float aspect, float znear, float zfar);
void dgk_gfx_look_at(dgk_v3 eye, dgk_v3 at, dgk_v3 up);
void dgk_gfx_draw_mesh(const dgk_mesh *m);
void dgk_gfx_overlay_begin(void);         /* 2D: 640x480 units, no depth */
void dgk_gfx_overlay_end(void);

#endif
