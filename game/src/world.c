/* world.c - loading worlds, the ground's height, box buckets, drawing
 * (see world.h). */
#include "world.h"
#include "dgk/log.h"
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int load_common(world *w, const char *path)
{
    const uint32_t *c = (const uint32_t *)dgk_pak_section(&w->pak, FW_FOURCC_COLL, NULL);
    const uint32_t *p = (const uint32_t *)dgk_pak_section(&w->pak, FW_FOURCC_PATH, NULL);
    const float *s = (const float *)dgk_pak_section(&w->pak, FW_FOURCC_SPAWN, NULL);
    if (!c || !p || !s) {
        dgk_log("FW-ERROR %s: boxes, path or start missing", path);
        return -1;
    }
    w->nboxes = (int)c[0];
    w->boxes = (const obb *)(c + 1);
    w->npath = (int)p[0];
    w->path = (const float *)(p + 1);
    w->spawn_x = s[0];
    w->spawn_y = s[1];
    w->spawn_heading = s[2];
    return 0;
}

/* Buckets of WORLD_GRID metres over the world (or the yard's +-160 m),
 * each box in every bucket its bounding circle touches. */
static int bucket_boxes(world *w)
{
    float origin = w->chunked ? 0.0f : -160.0f, span = w->chunked ? WG_SIZE : 320.0f;
    int n = (int)(span / WORLD_GRID) + 1, i, pass, total = 0;
    int *count;
    w->grid_n = n;
    count = (int *)calloc((size_t)n * n + 1, sizeof(int));
    w->bucket_first = (int *)calloc((size_t)n * n + 1, sizeof(int));
    if (!count || !w->bucket_first)
        return -1;
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < w->nboxes; i++) {
            const obb *b = &w->boxes[i];
            float r = sqrtf(b->hl * b->hl + b->hw * b->hw);
            int x0 = (int)floorf((b->cx - r - origin) / WORLD_GRID), x1 = (int)floorf((b->cx + r - origin) / WORLD_GRID);
            int y0 = (int)floorf((b->cy - r - origin) / WORLD_GRID), y1 = (int)floorf((b->cy + r - origin) / WORLD_GRID);
            int x, y;
            for (y = DGK_MAX(y0, 0); y <= DGK_MIN(y1, n - 1); y++)
                for (x = DGK_MAX(x0, 0); x <= DGK_MIN(x1, n - 1); x++) {
                    if (pass == 0)
                        count[y * n + x]++;
                    else
                        w->bucket_items[w->bucket_first[y * n + x] + count[y * n + x]++] = i;
                }
        }
        if (pass == 0) {
            for (i = 0; i < n * n; i++) {
                w->bucket_first[i] = total;
                total += count[i];
            }
            w->bucket_first[n * n] = total;
            w->bucket_items = (int *)malloc(sizeof(int) * (size_t)(total ? total : 1));
            memset(count, 0, sizeof(int) * (size_t)n * n);
        }
    }
    free(count);
    return 0;
}

int world_load(world *w, const char *path)
{
    memset(w, 0, sizeof *w);
    if (dgk_pak_load(&w->pak, path) != 0)
        return -1;
    w->hdr = (const wg_world *)dgk_pak_section(&w->pak, WG_WORLD, NULL);
    if (w->hdr) {
        const uint32_t *d = (const uint32_t *)dgk_pak_section(&w->pak, WG_DEPOTS, NULL);
        w->chunked = 1;
        w->chunks = (const wg_chunk *)dgk_pak_section(&w->pak, WG_CINDEX, NULL);
        w->verts = (const wg_vertex *)dgk_pak_section(&w->pak, WG_CVERTS, NULL);
        w->tris = (const uint16_t *)dgk_pak_section(&w->pak, WG_CTRIS, NULL);
        w->height = (const int16_t *)dgk_pak_section(&w->pak, WG_HEIGHT, NULL);
        if (!w->chunks || !w->verts || !w->tris || !w->height || !d || w->hdr->version != 1) {
            dgk_log("FW-ERROR %s: not a version 1 world", path);
            world_free(w);
            return -1;
        }
        w->ndepots = (int)d[0];
        w->depots = (const wg_depot *)(d + 1);
    } else {
        const uint8_t *m = (const uint8_t *)dgk_pak_section(&w->pak, FW_FOURCC_MESH, NULL);
        uint32_t nv, nt;
        if (!m) {
            dgk_log("FW-ERROR %s: no world and no yard in it", path);
            world_free(w);
            return -1;
        }
        memcpy(&nv, m, 4);
        memcpy(&nt, m + 4, 4);
        w->mesh.nverts = (int)nv;
        w->mesh.ntris = (int)nt;
        w->mesh.pos = (const float *)(m + 8);
        w->mesh.rgba = m + 8 + nv * 12;
        w->mesh.idx = (const uint16_t *)(m + 8 + nv * 16);
    }
    if (load_common(w, path) != 0 || bucket_boxes(w) != 0) {
        world_free(w);
        return -1;
    }
    dgk_log("FW-WORLD %s: %s, %d boxes, a path of %d points, %d depots, seed %lu", path,
            w->chunked ? "generated" : "test yard", w->nboxes, w->npath, w->ndepots, (unsigned long)w->pak.seed);
    return 0;
}

void world_free(world *w)
{
    free(w->bucket_first);
    free(w->bucket_items);
    dgk_pak_free(&w->pak);
    memset(w, 0, sizeof *w);
}

float world_height(const world *w, float x, float y)
{
    float gx, gy, fx, fy, a, b, c, d;
    int i, j, n;
    if (!w->chunked)
        return 0;
    n = (int)w->hdr->hn;
    gx = DGK_CLAMP(x / w->hdr->cell, 0.0f, n - 1.001f);
    gy = DGK_CLAMP(y / w->hdr->cell, 0.0f, n - 1.001f);
    i = (int)gx;
    j = (int)gy;
    fx = gx - i;
    fy = gy - j;
    a = w->height[j * n + i];
    b = w->height[j * n + i + 1];
    c = w->height[(j + 1) * n + i];
    d = w->height[(j + 1) * n + i + 1];
    /* The same split as the terrain's triangles (0-1-2, 0-2-3): exactly on them. */
    if (fx >= fy)
        return (a + (b - a) * fx + (d - b) * fy) * 0.1f;
    return (a + (d - c) * fx + (c - a) * fy) * 0.1f;
}

int world_boxes_near(const world *w, float x, float y, float r, const obb **out, int max)
{
    float origin = w->chunked ? 0.0f : -160.0f;
    int x0 = (int)floorf((x - r - origin) / WORLD_GRID), x1 = (int)floorf((x + r - origin) / WORLD_GRID);
    int y0 = (int)floorf((y - r - origin) / WORLD_GRID), y1 = (int)floorf((y + r - origin) / WORLD_GRID);
    int bx, by, k, n = 0, i;
    for (by = DGK_MAX(y0, 0); by <= DGK_MIN(y1, w->grid_n - 1); by++)
        for (bx = DGK_MAX(x0, 0); bx <= DGK_MIN(x1, w->grid_n - 1); bx++) {
            int cell = by * w->grid_n + bx;
            for (k = w->bucket_first[cell]; k < w->bucket_first[cell + 1] && n < max; k++) {
                const obb *b = &w->boxes[w->bucket_items[k]];
                for (i = 0; i < n && out[i] != b; i++)
                    continue;
                if (i == n)
                    out[n++] = b;                  /* each box once */
            }
        }
    return n;
}

void world_draw(world *w, float x, float y, float r)
{
    int cx0, cx1, cy0, cy1, cx, cy;
    w->drawn_chunks = 0;
    if (!w->chunked) {
        dgk_gfx_draw_mesh(&w->mesh);
        return;
    }
    cx0 = DGK_MAX((int)floorf((x - r) / WG_CHUNK), 0);
    cx1 = DGK_MIN((int)floorf((x + r) / WG_CHUNK), WG_CHUNKS - 1);
    cy0 = DGK_MAX((int)floorf((y - r) / WG_CHUNK), 0);
    cy1 = DGK_MIN((int)floorf((y + r) / WG_CHUNK), WG_CHUNKS - 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) {
            const wg_chunk *c = &w->chunks[cy * WG_CHUNKS + cx];
            const wg_vertex *v = w->verts + c->first_vertex;
            if (!c->ntris)
                continue;
            glPushMatrix();
            glTranslatef(cx * WG_CHUNK, 0, -cy * WG_CHUNK);
            glScalef(1.0f / WG_UNIT, 1.0f / WG_UNIT, 1.0f / WG_UNIT);
            glVertexPointer(3, GL_SHORT, sizeof *v, &v->x);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof *v, v->rgba);
            glDrawElements(GL_TRIANGLES, (GLsizei)c->ntris * 3, GL_UNSIGNED_SHORT, w->tris + c->first_index);
            glPopMatrix();
            dgk_gfx_tris += c->ntris;
            w->drawn_chunks++;
        }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
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
