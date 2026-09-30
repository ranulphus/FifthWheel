/* mesh.c - meshes with baked light (see mesh.h). */
#include "mesh.h"
#include <math.h>
#include <string.h>

void mb_init(builder *b, float *pos, uint8_t *rgba, uint16_t *idx, int max_verts, int max_tris)
{
    memset(b, 0, sizeof *b);
    b->pos = pos;
    b->rgba = rgba;
    b->idx = idx;
    b->max_verts = max_verts;
    b->max_tris = max_tris;
}

/* The sun, from the upper left of the screen when north is up: the game's
 * north-west, high. Faces away from it keep a strong ambient. */
static float sun(float nx, float ny, float nz)
{
    const float sx = -0.45f, sy = 0.75f, sz = -0.48f;
    float d = nx * sx + ny * sy + nz * sz;
    return 0.58f + 0.42f * (d > 0 ? d : 0);
}

uint32_t mb_shade(uint32_t rgb, float k)
{
    float r = ((rgb >> 16) & 255) * k, g = ((rgb >> 8) & 255) * k, bl = (rgb & 255) * k;
    return (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(bl > 255 ? 255 : bl);
}

void mb_quad_lit(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb,
                 float light)
{
    const float *p[4];
    uint32_t c = mb_shade(rgb, light);
    int i, base = b->nverts;
    if (b->nverts + 4 > b->max_verts || b->ntris + 2 > b->max_tris) {
        b->overflow = 1;
        return;
    }
    p[0] = p0; p[1] = p1; p[2] = p2; p[3] = p3;
    for (i = 0; i < 4; i++) {
        memcpy(&b->pos[b->nverts * 3], p[i], 3 * sizeof(float));
        b->rgba[b->nverts * 4 + 0] = (uint8_t)(c >> 16);
        b->rgba[b->nverts * 4 + 1] = (uint8_t)(c >> 8);
        b->rgba[b->nverts * 4 + 2] = (uint8_t)c;
        b->rgba[b->nverts * 4 + 3] = 255;
        b->nverts++;
    }
    b->idx[b->ntris * 3 + 0] = (uint16_t)base;
    b->idx[b->ntris * 3 + 1] = (uint16_t)(base + 1);
    b->idx[b->ntris * 3 + 2] = (uint16_t)(base + 2);
    b->idx[b->ntris * 3 + 3] = (uint16_t)base;
    b->idx[b->ntris * 3 + 4] = (uint16_t)(base + 2);
    b->idx[b->ntris * 3 + 5] = (uint16_t)(base + 3);
    b->ntris += 2;
}

void mb_quad(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb)
{
    float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    float vx = p3[0] - p0[0], vy = p3[1] - p0[1], vz = p3[2] - p0[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    mb_quad_lit(b, p0, p1, p2, p3, rgb, l > 0 ? sun(nx / l, ny / l, nz / l) : 1.0f);
}

void mb_box(builder *b, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb)
{
    float v[8][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
                      { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } };
    mb_quad(b, v[7], v[6], v[5], v[4], rgb);        /* top */
    mb_quad(b, v[3], v[2], v[6], v[7], rgb);        /* +z */
    mb_quad(b, v[1], v[0], v[4], v[5], rgb);        /* -z */
    mb_quad(b, v[2], v[1], v[5], v[6], rgb);        /* +x */
    mb_quad(b, v[0], v[3], v[7], v[4], rgb);        /* -x */
}

void mb_mesh(dgk_mesh *m, const builder *b)
{
    m->pos = b->pos;
    m->rgba = b->rgba;
    m->idx = b->idx;
    m->nverts = b->nverts;
    m->ntris = b->ntris;
}
