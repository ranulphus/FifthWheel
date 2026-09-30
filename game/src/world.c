/* world.c - loading the world pack; box overlap tests (see world.h). */
#include "world.h"
#include "dgk/log.h"
#include <math.h>
#include <string.h>

int world_load(world *w, const char *path)
{
    const uint8_t *m;
    const uint32_t *c, *p;
    const float *s;
    uint32_t size;
    memset(w, 0, sizeof *w);
    if (dgk_pak_load(&w->pak, path) != 0)
        return -1;
    m = (const uint8_t *)dgk_pak_section(&w->pak, FW_FOURCC_MESH, &size);
    c = (const uint32_t *)dgk_pak_section(&w->pak, FW_FOURCC_COLL, NULL);
    p = (const uint32_t *)dgk_pak_section(&w->pak, FW_FOURCC_PATH, NULL);
    s = (const float *)dgk_pak_section(&w->pak, FW_FOURCC_SPAWN, NULL);
    if (!m || !c || !p || !s) {
        dgk_log("FW-ERROR %s: sections missing", path);
        world_free(w);
        return -1;
    }
    {
        uint32_t nv, nt;
        memcpy(&nv, m, 4);
        memcpy(&nt, m + 4, 4);
        w->mesh.nverts = (int)nv;
        w->mesh.ntris = (int)nt;
        w->mesh.pos = (const float *)(m + 8);
        w->mesh.rgba = m + 8 + nv * 12;
        w->mesh.idx = (const uint16_t *)(m + 8 + nv * 16);
    }
    w->nboxes = (int)c[0];
    w->boxes = (const obb *)(c + 1);
    w->npath = (int)p[0];
    w->path = (const float *)(p + 1);
    w->spawn_x = s[0];
    w->spawn_y = s[1];
    w->spawn_heading = s[2];
    dgk_log("FW-WORLD %s: %d tris, %d boxes, path of %d points", path, w->mesh.ntris, w->nboxes, w->npath);
    return 0;
}

void world_free(world *w)
{
    dgk_pak_free(&w->pak);
    memset(w, 0, sizeof *w);
}

int obb_overlap(const obb *a, const obb *b, float *mx, float *my)
{
    float ax[4][2], best = 1e30f, bx = 0, by = 0, dx = b->cx - a->cx, dy = b->cy - a->cy;
    int i;
    ax[0][0] = cosf(a->angle); ax[0][1] = sinf(a->angle);
    ax[1][0] = -ax[0][1];      ax[1][1] = ax[0][0];
    ax[2][0] = cosf(b->angle); ax[2][1] = sinf(b->angle);
    ax[3][0] = -ax[2][1];      ax[3][1] = ax[2][0];
    for (i = 0; i < 4; i++) {
        float nx = ax[i][0], ny = ax[i][1];
        float ra = a->hl * fabsf(ax[0][0] * nx + ax[0][1] * ny) + a->hw * fabsf(ax[1][0] * nx + ax[1][1] * ny);
        float rb = b->hl * fabsf(ax[2][0] * nx + ax[2][1] * ny) + b->hw * fabsf(ax[3][0] * nx + ax[3][1] * ny);
        float d = dx * nx + dy * ny, o = ra + rb - fabsf(d);
        if (o <= 0)
            return 0;
        if (o < best) {
            best = o;
            bx = d > 0 ? -nx * o : nx * o;     /* push a away from b */
            by = d > 0 ? -ny * o : ny * o;
        }
    }
    *mx = bx;
    *my = by;
    return 1;
}
