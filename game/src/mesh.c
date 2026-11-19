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
float mb_light(float nx, float ny, float nz)
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

void mb_normals(builder *b, float *nrm, uint32_t *base)
{
    b->nrm = nrm;
    b->base = base;
}

static void set_rgb(uint8_t *rgba, uint32_t c)
{
    rgba[0] = (uint8_t)(c >> 16);
    rgba[1] = (uint8_t)(c >> 8);
    rgba[2] = (uint8_t)c;
    rgba[3] = 255;
}

/* A vertex lit `light`, keeping its normal n (if the builder keeps them). */
static int put_vertex(builder *b, const float *p, const float *n, uint32_t rgb, float light)
{
    if (b->nverts >= b->max_verts) {
        b->overflow = 1;
        return 0;
    }
    memcpy(&b->pos[b->nverts * 3], p, 3 * sizeof(float));
    set_rgb(&b->rgba[b->nverts * 4], mb_shade(rgb, light));
    if (b->nrm) {
        memcpy(&b->nrm[b->nverts * 3], n, 3 * sizeof(float));
        b->base[b->nverts] = rgb;
    }
    return b->nverts++;
}

int mb_vertex(builder *b, const float *p, const float *n, uint32_t rgb)
{
    return put_vertex(b, p, n, rgb, mb_light(n[0], n[1], n[2]));
}

void mb_tri(builder *b, int v0, int v1, int v2)
{
    if (b->ntris >= b->max_tris) {
        b->overflow = 1;
        return;
    }
    b->idx[b->ntris * 3 + 0] = (uint16_t)v0;
    b->idx[b->ntris * 3 + 1] = (uint16_t)v1;
    b->idx[b->ntris * 3 + 2] = (uint16_t)v2;
    b->ntris++;
}

static void quad_n(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb,
                   const float *n, float light)
{
    const float *p[4];
    int i, base = b->nverts;
    if (b->nverts + 4 > b->max_verts || b->ntris + 2 > b->max_tris) {
        b->overflow = 1;
        return;
    }
    p[0] = p0; p[1] = p1; p[2] = p2; p[3] = p3;
    for (i = 0; i < 4; i++)
        put_vertex(b, p[i], n, rgb, light);
    b->idx[b->ntris * 3 + 0] = (uint16_t)base;
    b->idx[b->ntris * 3 + 1] = (uint16_t)(base + 1);
    b->idx[b->ntris * 3 + 2] = (uint16_t)(base + 2);
    b->idx[b->ntris * 3 + 3] = (uint16_t)base;
    b->idx[b->ntris * 3 + 4] = (uint16_t)(base + 2);
    b->idx[b->ntris * 3 + 5] = (uint16_t)(base + 3);
    b->ntris += 2;
}

void mb_quad_lit(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb,
                 float light)
{
    static const float up[3] = { 0, 1, 0 };
    quad_n(b, p0, p1, p2, p3, rgb, up, light);
}

void mb_quad(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb)
{
    float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    float vx = p3[0] - p0[0], vy = p3[1] - p0[1], vz = p3[2] - p0[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz), n[3];
    n[0] = l > 0 ? nx / l : 0;
    n[1] = l > 0 ? ny / l : 1;
    n[2] = l > 0 ? nz / l : 0;
    quad_n(b, p0, p1, p2, p3, rgb, n, l > 0 ? mb_light(n[0], n[1], n[2]) : 1.0f);
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

/* A quad of four vertex indices; two of them the same (at a rounded box's
 * pole): the one triangle. */
static void quad_idx(builder *b, int a, int c, int d, int e)
{
    if (a == c || c == d)
        mb_tri(b, a, d, e);
    else if (d == e || e == a)
        mb_tri(b, a, c, d);
    else {
        mb_tri(b, a, c, d);
        mb_tri(b, a, d, e);
    }
}

void mb_rbox(builder *b, float x0, float y0, float z0, float x1, float y1, float z1, float r, int k, uint32_t rgb)
{
    /* A sphere's grid of 4(k + 1) columns round and 2(k + 1) rows up, each
     * quarter its own (the seams between quarters are the box's flat faces),
     * each vertex pushed out to its quarter's corner of the inner box. The
     * poles' rows meet at one vertex a quarter; the top and bottom faces'
     * middles are a quad each. */
    enum { KMAX = 4 };
    int grid[2 * (KMAX + 1)][4 * (KMAX + 1)], rows, cols, t, c;
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, cz = (z0 + z1) * 0.5f, hx, hy, hz;
    if (k <= 0 || r <= 0) {
        mb_box(b, x0, y0, z0, x1, y1, z1, rgb);
        return;
    }
    k = k > KMAX ? KMAX : k;
    hx = (x1 - x0) * 0.5f - r;
    hy = (y1 - y0) * 0.5f - r;
    hz = (z1 - z0) * 0.5f - r;
    hx = hx > 0 ? hx : 0;
    hy = hy > 0 ? hy : 0;
    hz = hz > 0 ? hz : 0;
    rows = 2 * (k + 1);
    cols = 4 * (k + 1);
    for (t = 0; t < rows; t++) {
        int up = t > k, u = t % (k + 1);
        float phi = (up ? 0.0f : -1.5707963f) + 1.5707963f * u / k;
        for (c = 0; c < cols; c++) {
            int q = c / (k + 1), s = c % (k + 1);
            float a = 1.5707963f * q + 1.5707963f * s / k, n[3], p[3];
            float sx = (q == 0 || q == 3) ? 1.0f : -1.0f, sz = q < 2 ? 1.0f : -1.0f;
            if ((t == 0 || t == rows - 1) && s > 0) {
                grid[t][c] = grid[t][c - s];             /* the pole: one vertex a quarter */
                continue;
            }
            n[0] = cosf(phi) * cosf(a);
            n[1] = sinf(phi);
            n[2] = cosf(phi) * sinf(a);
            if (t == 0 || t == rows - 1) {
                n[0] = n[2] = 0;
                n[1] = up ? 1.0f : -1.0f;
            }
            p[0] = cx + sx * hx + r * n[0];
            p[1] = cy + (up ? hy : -hy) + r * n[1];
            p[2] = cz + sz * hz + r * n[2];
            grid[t][c] = mb_vertex(b, p, n, rgb);
        }
    }
    /* Outward faces: anticlockwise seen from outside. */
    for (t = 0; t + 1 < rows; t++)
        for (c = 0; c < cols; c++) {
            int n = (c + 1) % cols;
            quad_idx(b, grid[t][c], grid[t + 1][c], grid[t + 1][n], grid[t][n]);
        }
    quad_idx(b, grid[rows - 1][0], grid[rows - 1][k + 1], grid[rows - 1][2 * (k + 1)], grid[rows - 1][3 * (k + 1)]);
    quad_idx(b, grid[0][3 * (k + 1)], grid[0][2 * (k + 1)], grid[0][k + 1], grid[0][0]);
}

void mb_cylinder(builder *b, int axis, const float *c, float radius, float half, int n, uint32_t side, uint32_t ends,
                 int which)
{
    enum { NMAX = 24 };
    int ring[2][NMAX], i, e, u = (axis + 1) % 3, v = (axis + 2) % 3;
    n = n < 3 ? 3 : n > NMAX ? NMAX : n;
    for (e = 0; e < 2; e++)                           /* the rims: e 0 the - end, 1 the + end */
        for (i = 0; i < n && half > 0; i++) {
            float a = 6.2831853f * i / n, p[3], nm[3];
            nm[axis] = 0;
            nm[u] = cosf(a);
            nm[v] = sinf(a);
            p[axis] = c[axis] + (e ? half : -half);
            p[u] = c[u] + radius * nm[u];
            p[v] = c[v] + radius * nm[v];
            ring[e][i] = mb_vertex(b, p, nm, side);
        }
    for (i = 0; i < n && half > 0; i++) {
        int j = (i + 1) % n;
        quad_idx(b, ring[0][i], ring[0][j], ring[1][j], ring[1][i]);
    }
    for (e = 0; e < 2; e++) {                         /* the ends: fans, flat */
        int f[NMAX];
        float nm[3] = { 0, 0, 0 };
        if (!(which & (e ? 1 : 2)))
            continue;
        nm[axis] = e ? 1.0f : -1.0f;
        for (i = 0; i < n; i++) {
            float a = 6.2831853f * i / n, p[3];
            p[axis] = c[axis] + (e ? half : -half);
            p[u] = c[u] + radius * cosf(a);
            p[v] = c[v] + radius * sinf(a);
            f[i] = mb_vertex(b, p, nm, ends);
        }
        for (i = 1; i + 1 < n; i++)
            if (e)
                mb_tri(b, f[0], f[i], f[i + 1]);
            else
                mb_tri(b, f[0], f[i + 1], f[i]);
    }
}

/* A toy's plastic sheen: where a surface faces halfway between the sun and
 * straight up (the camera looks down from high), a glint of white. */
#define GLOSS 0.5f
#define GLOSS_POWER 4                 /* squarings: the glint's tightness (2^4 = 16) */

void mb_relight(const builder *b, float yaw)
{
    const float hx = -0.24f, hy = 0.935f, hz = -0.257f;
    float c = cosf(yaw), s = sinf(yaw);
    int i, k;
    if (!b->nrm)
        return;
    for (i = 0; i < b->nverts; i++) {
        const float *n = &b->nrm[i * 3];
        float wx = n[0] * c + n[2] * s, wz = -n[0] * s + n[2] * c, h = wx * hx + n[1] * hy + wz * hz;
        uint32_t rgb = mb_shade(b->base[i], mb_light(wx, n[1], wz)), out = 0;
        h = h > 0 ? h : 0;
        for (k = 0; k < GLOSS_POWER; k++)
            h *= h;
        for (k = 0; k <= 16; k += 8) {
            float v = ((rgb >> k) & 255) + 255.0f * GLOSS * h;
            out |= (uint32_t)(v > 255 ? 255 : v) << k;
        }
        set_rgb(&b->rgba[i * 4], out);
    }
}

