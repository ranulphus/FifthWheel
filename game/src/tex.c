/* tex.c - HIGH's textures: the atlas (see tex.h). */
#include "tex.h"
#include "wgen.h"
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#define LEVELS 3                      /* 512, 256, 128: past that the tiles' margins blur into each other */

/* The levels while they are drawn (malloc'd at HIGH only, and freed once GL has them: as static arrays they
 * would cost every preset 3 MB). */
static uint8_t *level[LEVELS];

/* A repeatable hash of a texel's coordinates (and a salt), 0..1. */
static float h01(int x, int y, int salt)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

/* The patterns, light values 0..1 (1: the vertex's colour as it is), at
 * (x, y) in a tile's 128 texels; p, q the same over the mapped 120 (0..119),
 * so a pattern that repeats with that period joins its neighbours' (the
 * ground's cells, one after another). */
static float shade(int tile, int x, int y)
{
    const int in = WG_TILE - 2 * WG_GUTTER;
    int p = ((x - WG_GUTTER) % in + in) % in, q = ((y - WG_GUTTER) % in + in) % in;
    float v = 1.0f;
    int k;
    switch (tile) {
    case WG_TX_GRASS: {               /* soft patches of lighter and darker grass, tufts on them */
        float sum = 0;
        for (k = 0; k < 9; k++) {             /* the patches: round, soft-edged, wrapping with the tile */
            float cx = h01(k, 0, 1) * in, cy = h01(k, 1, 1) * in, r = 14.0f + 14.0f * h01(k, 2, 1);
            float dx = fabsf((float)p - cx), dy = fabsf((float)q - cy), d;
            dx = DGK_MIN(dx, in - dx);
            dy = DGK_MIN(dy, in - dy);
            d = sqrtf(dx * dx + dy * dy) / r;
            if (d < 1.0f)
                sum += (k & 1 ? 0.09f : -0.09f) * (1.0f - d * d);
        }
        v = 0.92f + sum;
        for (k = 0; k < 26; k++) {            /* tufts: dark V's, 8 texels tall */
            int cx = (int)(h01(k, 0, 2) * in), cy = (int)(h01(k, 1, 2) * in);
            int dx = ((p - cx) % in + in + in / 2) % in - in / 2, dy = ((q - cy) % in + in + in / 2) % in - in / 2;
            if (dy >= -8 && dy <= 0 && (abs(2 * dx + dy) <= 1 || abs(2 * dx - dy) <= 1 || (dx == 0 && dy >= -7)))
                v = 0.72f;
        }
        break;
    }
    case WG_TX_CROPS:                 /* rows of crop and furrows between them, one of four ways (by the field) */
    case WG_TX_CROPS_45:
    case WG_TX_CROPS_90:
    case WG_TX_CROPS_135: {
        /* Across the rows (a period of 15 that divides 120, so each way repeats with the ground's cells), and
         * along them, for the plants' dots. */
        int way = tile - WG_TX_CROPS;
        int a = way == 0 ? q : way == 1 ? p + q : way == 2 ? p : p - q + 120;
        int b = way == 0 ? p : way == 1 ? p - q + 120 : way == 2 ? q : p + q;
        v = (a % 15) < 3 ? 0.76f : 0.95f + 0.05f * (h01(b / 3, a / 15, 4) - 0.5f);
        if ((a % 15) >= 6 && (a % 15) <= 10 && (b % 8) < 4)
            v = 0.86f;
        break;
    }
    case WG_TX_ASPHALT:               /* grit */
        v = h01(p, q, 5) < 0.07f ? 0.84f : h01(p, q, 6) < 0.04f ? 1.0f : 0.95f;
        break;
    case WG_TX_BRICK: {               /* bricks, every other row shifted half one; light mortar */
        int row = y / 10, col = (x + (row & 1) * 12) / 24, by = y % 10, bx = (x + (row & 1) * 12) % 24;
        v = by < 2 || bx < 2 ? 1.0f : 0.82f + 0.08f * h01(col, row, 7) + (by == 2 ? 0.08f : 0.0f);
        break;
    }
    case WG_TX_BOARDS: {              /* weatherboard: each board lighter at its top, a shadow under it */
        int by = y % 12;
        v = by >= 10 ? 0.78f : 0.9f + 0.1f * (1.0f - by / 10.0f);
        break;
    }
    case WG_TX_ROOF: {                /* scalloped tiles in rows, each row half a tile along */
        int row = y / 12, sx = (x + (row & 1) * 8) % 16, sy = y % 12;
        float dx = (float)sx - 7.5f, d = sqrtf(dx * dx + (float)(sy - 2) * (sy - 2));
        v = sy < 2 ? 0.98f : d > 7.0f ? 0.74f : d > 6.0f ? 0.82f : 0.92f + 0.08f * (1.0f - sy / 12.0f);
        break;
    }
    case WG_TX_LEAVES:                /* clumps of leaves, each lit from the top left */
        v = 0.82f;
        for (k = 0; k < 40; k++) {
            float cx = h01(k, 0, 8) * WG_TILE, cy = h01(k, 1, 8) * WG_TILE, r = 7.0f + 7.0f * h01(k, 2, 8);
            float dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 < r * r)
                v = 0.84f + 0.16f * (1.0f - (dx + dy + r * 1.4f) / (r * 2.8f));
        }
        break;
    case WG_TX_PAVING:                /* square slabs with joints */
        v = p % 30 < 2 || q % 30 < 2 ? 0.8f : 0.95f + 0.05f * (h01(p / 30, q / 30, 9) - 0.5f);
        break;
    default:
        v = 1.0f;
        break;
    }
    return v;
}

unsigned int tex_atlas(void)
{
    GLuint t;
    int x, y, l, size;
    for (l = 0, size = WG_ATLAS; l < LEVELS; l++, size /= 2)
        if (!(level[l] = malloc((size_t)size * size * 4))) {
            while (l-- > 0)
                free(level[l]);
            return 0;
        }
    for (y = 0; y < WG_ATLAS; y++)
        for (x = 0; x < WG_ATLAS; x++) {
            int tile = (y / WG_TILE) * (WG_ATLAS / WG_TILE) + x / WG_TILE;
            float v = tile < WG_TX_COUNT ? shade(tile, x % WG_TILE, y % WG_TILE) : 1.0f;
            uint8_t *o = &level[0][(y * WG_ATLAS + x) * 4];
            o[0] = o[1] = o[2] = (uint8_t)(255.0f * (v < 0 ? 0 : v > 1 ? 1 : v) + 0.5f);
            o[3] = 255;
        }
    /* Each smaller level the last's 2 x 2 means, its pattern a little fainter (a fifth nearer 0.9 each level):
     * far off, rows and tufts thinner than a pixel shimmer less. */
    for (l = 1, size = WG_ATLAS / 2; l < LEVELS; l++, size /= 2)
        for (y = 0; y < size; y++)
            for (x = 0; x < size; x++) {
                int c, w = size * 2;
                for (c = 0; c < 4; c++) {
                    int sum = level[l - 1][((2 * y) * w + 2 * x) * 4 + c] + level[l - 1][((2 * y) * w + 2 * x + 1) * 4 + c] +
                              level[l - 1][((2 * y + 1) * w + 2 * x) * 4 + c] +
                              level[l - 1][((2 * y + 1) * w + 2 * x + 1) * 4 + c];
                    float m = sum / 4.0f;
                    if (c < 3)
                        m = 229.5f + (m - 229.5f) * 0.8f;
                    level[l][(y * size + x) * 4 + c] = (uint8_t)(m + 0.5f);
                }
            }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, LEVELS - 1);
    {
        /* The ground seen at a slant: anisotropic filtering where the card has it (the Radeons, under DOS-GL). */
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        if (ext && strstr(ext, "GL_EXT_texture_filter_anisotropic")) {
            GLfloat most = 1;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, most < 4 ? most : 4);
        }
    }
    for (l = 0, size = WG_ATLAS; l < LEVELS; l++, size /= 2)
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, level[l]);
    for (l = 0; l < LEVELS; l++) {
        free(level[l]);
        level[l] = NULL;
    }
    return t;
}
