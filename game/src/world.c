/* world.c - loading worlds, the ground's height, box buckets, drawing
 * (see world.h). */
#include "world.h"
#include "dgk/app.h"
#include "dgk/log.h"
#define GL_GLEXT_PROTOTYPES                  /* (DOS-GL's buffer objects: GL_ARB_vertex_buffer_object) */
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
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

int world_load_hi(world *w)
{
    char hp[sizeof w->file + 4];
    const char *dot;
    const wg_hi *h;
    uint32_t nv = 0, nt = 0;
    if (w->hi_state)
        return w->hi_state > 0;
    w->hi_state = -1;
    if (!w->chunked)
        return 0;
    dot = strrchr(w->file, '.');
    snprintf(hp, sizeof hp, "%.*sHI%s", dot ? (int)(dot - w->file) : (int)strlen(w->file), w->file, dot ? dot : "");
    if (dgk_pak_load(&w->hipak, hp) != 0) {
        dgk_log("FW-WORLD %s: none, so HIGH draws untextured", hp);
        return 0;
    }
    h = (const wg_hi *)dgk_pak_section(&w->hipak, WG_HI_HEADER, NULL);
    w->hi_index = (const wg_chunk *)dgk_pak_section(&w->hipak, WG_HI_INDEX, NULL);
    w->hi_verts = (const wg_hvertex *)dgk_pak_section(&w->hipak, WG_HI_VERTS, NULL);
    w->hi_tris = (const uint16_t *)dgk_pak_section(&w->hipak, WG_HI_TRIS, NULL);
    dgk_pak_section(&w->pak, WG_CVERTS, &nv);
    dgk_pak_section(&w->pak, WG_CTRIS, &nt);
    if (!h || !w->hi_index || !w->hi_verts || !w->hi_tris || h->version != WG_HI_VERSION ||
        h->seed != w->pak.seed || h->main_verts != nv / sizeof(wg_vertex) || h->main_tris != nt / 6) {
        dgk_log("FW-WORLD %s: not made with %s, so HIGH draws untextured", hp, w->file);
        dgk_pak_free(&w->hipak);
        return 0;
    }
    w->hi_state = 1;
    dgk_log("FW-WORLD %s: HIGH's textured world", hp);
    return 1;
}

int world_load(world *w, const char *path)
{
    memset(w, 0, sizeof *w);
    snprintf(w->file, sizeof w->file, "%s", path);
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
        w->variants = (const wg_variant *)dgk_pak_section(&w->pak, WG_CVARIANT, NULL);
        if (!w->chunks || !w->verts || !w->tris || !w->height || !d || w->hdr->version != WG_VERSION ||
            w->hdr->chunks != WG_CHUNKS) {
            dgk_log("FW-ERROR %s: not a version %d world", path, WG_VERSION);
            world_free(w);
            return -1;
        }
        w->ndepots = (int)d[0];
        w->depots = (const wg_depot *)(d + 1);
        {
            const uint32_t *tn = (const uint32_t *)dgk_pak_section(&w->pak, WG_TOWNS, NULL);
            const uint32_t *st = (const uint32_t *)dgk_pak_section(&w->pak, WG_STOPS, NULL);
            if (tn && st) {
                w->ntowns = (int)tn[0];
                w->towns = (const wg_town *)(tn + 1);
                w->nstops = (int)st[0];
                w->stops = (const wg_stop *)(st + 1);
            }
        }
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
    if (w->chunked && w->npath > 3) {
        /* The tour keeps left (WG_LANE), and so does the start: the pack's is the tour's third point. */
        w->lane = (float *)malloc(sizeof(float) * 2 * (size_t)w->npath);
        if (!w->lane) {
            world_free(w);
            return -1;
        }
        wg_offset((const float (*)[2])w->path, w->npath, WG_LANE, 1, (float (*)[2])w->lane);
        w->path = w->lane;
        w->spawn_x = w->lane[4];
        w->spawn_y = w->lane[5];
        w->spawn_heading = atan2f(w->lane[7] - w->lane[5], w->lane[6] - w->lane[4]);
    }
    dgk_log("FW-WORLD %s: %s, %d boxes, a path of %d points, %d depots, seed %lu, %d towns, %d parcel stops", path,
            w->chunked ? "generated" : "test yard", w->nboxes, w->npath, w->ndepots, (unsigned long)w->pak.seed,
            w->ntowns, w->nstops);
    return 0;
}

void world_free(world *w)
{
    if (w->vbo)
        glDeleteBuffersARB(1, &w->vbo);
    if (w->hi_vbo)
        glDeleteBuffersARB(1, &w->hi_vbo);
    free(w->bucket_first);
    free(w->bucket_items);
    free(w->lane);
    dgk_pak_free(&w->pak);
    if (w->hi_state > 0)
        dgk_pak_free(&w->hipak);
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
    for (i = 0; i < w->nprops && n < max; i++) {
        const obb *b = &w->props[i];
        float d = r + b->hl + b->hw;
        if ((b->cx - x) * (b->cx - x) + (b->cy - y) * (b->cy - y) < d * d)
            out[n++] = b;
    }
    return n;
}

const wg_graph *world_graph(const world *w, const wg_node **n, const wg_edge **e, const float **p)
{
    const uint8_t *b = w->chunked ? (const uint8_t *)dgk_pak_section(&w->pak, WG_GRAPH, NULL) : NULL;
    const wg_graph *g = (const wg_graph *)b;
    if (!g)
        return NULL;
    *n = (const wg_node *)(b + sizeof(wg_graph));
    *e = (const wg_edge *)(b + sizeof(wg_graph) + sizeof(wg_node) * g->nnodes);
    *p = (const float *)(b + sizeof(wg_graph) + sizeof(wg_node) * g->nnodes + sizeof(wg_edge) * g->nedges);
    return g;
}

/* The view's six planes (a x + b h + c z + d >= 0 inside), in GL's world
 * coordinates, from projection x modelview (Gribb and Hartmann). */
static void frustum(float plane[6][4])
{
    float p[16], mv[16], m[16];
    int i, j, k;
    glGetFloatv(GL_PROJECTION_MATRIX, p);
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    for (j = 0; j < 4; j++)                          /* column-major: m = p mv */
        for (i = 0; i < 4; i++) {
            float sum = 0;
            for (k = 0; k < 4; k++)
                sum += p[k * 4 + i] * mv[j * 4 + k];
            m[j * 4 + i] = sum;
        }
    for (k = 0; k < 6; k++) {                        /* left, right, bottom, top, near, far */
        int row = k / 2;
        float sgn = (k & 1) ? -1.0f : 1.0f;
        for (j = 0; j < 4; j++)
            plane[k][j] = m[j * 4 + 3] + sgn * m[j * 4 + row];
    }
}

/* A box wholly outside one of the planes cannot be seen. */
static int box_hidden(const float plane[6][4], float x0, float h0, float z0, float x1, float h1, float z1)
{
    int k, i;
    for (k = 0; k < 6; k++) {
        for (i = 0; i < 8; i++) {
            float x = i & 1 ? x1 : x0, h = i & 2 ? h1 : h0, z = i & 4 ? z1 : z0;
            if (plane[k][0] * x + plane[k][1] * h + plane[k][2] * z + plane[k][3] >= 0)
                break;
        }
        if (i == 8)
            return 1;
    }
    return 0;
}

/* A pack's vertices in a GL_STATIC_DRAW buffer, bound (made the first time). Each chunk is then drawn with its
 * arrays offset to its own first vertex, so the engine keeps each chunk's vertices on their own (DOS-GL's
 * advice: one buffer drawn by index offsets would make every chunk's span the whole world). */
static void bind_vbo(unsigned int *vbo, const dgk_pak *pak, uint32_t fourcc)
{
    if (!*vbo) {
        uint32_t size = 0;
        const void *data = dgk_pak_section(pak, fourcc, &size);
        glGenBuffersARB(1, vbo);
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, *vbo);
        glBufferDataARB(GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB);
        dgk_log("FW-WORLD %u KB of vertices in a buffer object", (unsigned)(size / 1024));
    }
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, *vbo);
}

void world_draw(world *w, float x, float y, float r)
{
    const char *hbase = (const char *)w->hi_verts, *base = (const char *)w->verts;
    const float margin = 8.0f;                       /* triangles go to chunks by centroid: some reach over */
    float plane[6][4];
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
    frustum(plane);
    w->culled_chunks = 0;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (w->engine) {                                 /* the buffer's offsets in place of the pack's addresses */
        if (w->textured) {
            bind_vbo(&w->hi_vbo, &w->hipak, WG_HI_VERTS);
            hbase = NULL;
        } else {
            bind_vbo(&w->vbo, &w->pak, WG_CVERTS);
            base = NULL;
        }
    }
    if (w->textured) {                               /* HIGH: WORLDHI.PAK's, the atlas tinted by the colours */
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, w->atlas);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glMatrixMode(GL_TEXTURE);                    /* its coordinates are texels */
        glLoadIdentity();
        glScalef(1.0f / WG_ATLAS, 1.0f / WG_ATLAS, 1.0f);
        glMatrixMode(GL_MODELVIEW);
    }
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) {
            const wg_chunk *c = &w->chunks[cy * WG_CHUNKS + cx];
            const char *v = base + (size_t)c->first_vertex * sizeof(wg_vertex);
            uint32_t first = 0, n = c->ntris;
            if (w->textured) {                       /* HIGH's, all of them */
                first = 0;
                n = w->hi_index[cy * WG_CHUNKS + cx].ntris;
            } else if (w->variants) {                /* this detail's runs, one stretch of them (wgen.h) */
                const wg_variant *var = &w->variants[cy * WG_CHUNKS + cx];
                if (w->models == 0)
                    n = c->ntris - var->medium_tris - var->high_tris;
                else {
                    first = var->low_tris;
                    n = c->ntris - var->low_tris - (w->models == 1 ? var->high_tris : 0);
                }
            }
            if (!n)
                continue;
            if (box_hidden(plane, cx * WG_CHUNK - margin, c->hmin / 10.0f - 1.0f, -((cy + 1) * WG_CHUNK + margin),
                           (cx + 1) * WG_CHUNK + margin, c->hmax / 10.0f + 1.0f, -(cy * WG_CHUNK - margin))) {
                w->culled_chunks++;
                continue;
            }
            glPushMatrix();
            glTranslatef(cx * WG_CHUNK, 0, -cy * WG_CHUNK);
            glScalef(1.0f / WG_UNIT, 1.0f / WG_UNIT, 1.0f / WG_UNIT);
            if (w->textured) {
                const wg_chunk *hc = &w->hi_index[cy * WG_CHUNKS + cx];
                const char *hv = hbase + (size_t)hc->first_vertex * sizeof(wg_hvertex);
                glVertexPointer(3, GL_SHORT, sizeof(wg_hvertex), hv + offsetof(wg_hvertex, x));
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(wg_hvertex), hv + offsetof(wg_hvertex, rgba));
                glTexCoordPointer(2, GL_SHORT, sizeof(wg_hvertex), hv + offsetof(wg_hvertex, s));
                glDrawElements(GL_TRIANGLES, (GLsizei)n * 3, GL_UNSIGNED_SHORT, w->hi_tris + hc->first_index);
            } else {
                glVertexPointer(3, GL_SHORT, sizeof(wg_vertex), v + offsetof(wg_vertex, x));
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(wg_vertex), v + offsetof(wg_vertex, rgba));
                glDrawElements(GL_TRIANGLES, (GLsizei)n * 3, GL_UNSIGNED_SHORT, w->tris + c->first_index + first * 3);
            }
            glPopMatrix();
            dgk_gfx_tris += n;
            dgk_gfx_draws++;
            w->drawn_chunks++;
            dgk_service_if_due();                    /* the audio thread's turn on a slow frame (DOS) */
        }
    if (w->textured) {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_TEXTURE_2D);
    }
    if (w->engine)
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
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
