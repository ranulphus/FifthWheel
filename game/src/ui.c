/* ui.c - the HUD's and menus' shapes (see ui.h). */
#include "ui.h"
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <math.h>
#include <string.h>

#define UI_PI 3.14159265f

/* ---- the texture ------------------------------------------------------------- */

/* 256 x 256 texels, RGBA: the round parts, each in its own square with a
 * clear margin (the filtering reads a texel past the edge). */
#define ATLAS 256

typedef struct sprite {
    int x, y, size;                   /* its square, in texels */
    float px;                         /* pixels on screen a texel */
} sprite;

enum { SP_DIAL, SP_MAP, SP_DISC, SP_COIN, SP_MEDAL };
static const sprite sprites[] = {
    { 0, 0, 128, 1.0f },              /* the dial: a texel a pixel */
    { 128, 0, 128, 68.0f / 63.0f },   /* the minimap's frame, radius 68 in 63 texels */
    { 0, 128, 32, 1.0f },             /* a white disc, radius 15: tinted, and panels' corners */
    { 32, 128, 32, 1.0f },            /* the coin, radius 10 */
    { 64, 128, 64, 1.0f },            /* the medallion, radius 18 */
};
#define DISC_R 15.0f
#define COIN_R 10.0f

static uint8_t atlas[ATLAS * ATLAS * 4];
static GLuint texture;

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* The square's texels all `rgb`, clear: what the filtering blends an edge
 * with, so the shape's own outermost colour (no fringe of another). */
static void clear(int s, uint32_t rgb)
{
    const sprite *p = &sprites[s];
    int i, j;
    for (j = p->y; j < p->y + p->size; j++)
        for (i = p->x; i < p->x + p->size; i++) {
            uint8_t *t = &atlas[(j * ATLAS + i) * 4];
            t[0] = (uint8_t)(rgb >> 16);
            t[1] = (uint8_t)(rgb >> 8);
            t[2] = (uint8_t)rgb;
            t[3] = 0;
        }
}

/* Paint into square s a ring from r0 to r1 texels about (cx, cy) (the
 * square's own texels), between angles lo and hi degrees (as ui_arc's, in
 * degrees; lo == hi: all round), in rgba: over what is there, or replacing
 * it. Each texel's cover comes from its centre's distance to the edges. */
static void paint(int s, float cx, float cy, float r0, float r1, float lo, float hi, uint32_t rgba, int replace)
{
    const sprite *p = &sprites[s];
    float sr = (float)(rgba >> 24), sg = (float)((rgba >> 16) & 255), sb = (float)((rgba >> 8) & 255);
    float sa = (float)(rgba & 255) / 255.0f;
    int i, j;
    for (j = 0; j < p->size; j++)
        for (i = 0; i < p->size; i++) {
            uint8_t *t = &atlas[((p->y + j) * ATLAS + p->x + i) * 4];
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float cover = clamp01(r1 - d + 0.5f) * (r0 > 0 ? clamp01(d - r0 + 0.5f) : 1.0f), da, a;
            if (lo != hi && cover > 0) {
                float mid = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
                da = atan2f(-dy, dx) * 180.0f / UI_PI - mid;
                while (da > 180)
                    da -= 360;
                while (da < -180)
                    da += 360;
                cover *= clamp01((half - fabsf(da)) * UI_PI / 180.0f * d + 0.5f);
            }
            if (cover <= 0)
                continue;
            if (replace) {
                t[0] = (uint8_t)(t[0] + (sr - t[0]) * cover + 0.5f);
                t[1] = (uint8_t)(t[1] + (sg - t[1]) * cover + 0.5f);
                t[2] = (uint8_t)(t[2] + (sb - t[2]) * cover + 0.5f);
                t[3] = (uint8_t)(t[3] + (sa * 255.0f - t[3]) * cover + 0.5f);
            } else {
                float a0 = sa * cover, ad = t[3] / 255.0f;
                a = a0 + ad * (1 - a0);
                t[0] = (uint8_t)((sr * a0 + t[0] * ad * (1 - a0)) / a + 0.5f);
                t[1] = (uint8_t)((sg * a0 + t[1] * ad * (1 - a0)) / a + 0.5f);
                t[2] = (uint8_t)((sb * a0 + t[2] * ad * (1 - a0)) / a + 0.5f);
                t[3] = (uint8_t)(a * 255.0f + 0.5f);
            }
        }
}

int ui_init(void)
{
    const float c = 64.0f, k = sprites[SP_MAP].px;
    int i;
    memset(atlas, 0, sizeof atlas);
    /* The dial: its shadow, the white rim, the face, the revs' track, a tick every 20 km/h. */
    clear(SP_DIAL, 0x000000u);
    paint(SP_DIAL, c + 3, c + 4, 0, UI_DIAL_R + 4, 0, 0, 0x00000050u, 0);
    paint(SP_DIAL, c, c, 0, UI_DIAL_R + 4, 0, 0, 0xFFFFFFFFu, 0);
    paint(SP_DIAL, c, c, 0, UI_DIAL_R, 0, 0, 0x1C2E5AFFu, 0);
    paint(SP_DIAL, c, c, UI_DIAL_R - 10, UI_DIAL_R - 3, UI_DIAL_A1, UI_DIAL_A0, 0x2E4478FFu, 0);
    for (i = 0; i <= 5; i++) {
        float a = UI_DIAL_A0 + (UI_DIAL_A1 - UI_DIAL_A0) * i / 5.0f;
        paint(SP_DIAL, c, c, UI_DIAL_R - 18, UI_DIAL_R - 12, a - 1.43f, a + 1.43f, 0xFFFFFFC0u, 0);
    }
    /* The minimap's frame: the deep blue ring, the white rim, the disc (translucent: the world shows through). */
    clear(SP_MAP, 0x1C2E5Au);
    paint(SP_MAP, c, c, 0, (UI_MAP_R + 6) / k, 0, 0, 0x1C2E5AFFu, 0);
    paint(SP_MAP, c, c, 0, (UI_MAP_R + 3) / k, 0, 0, 0xFFFFFFFFu, 1);
    paint(SP_MAP, c, c, 0, UI_MAP_R / k, 0, 0, 0x1E3A28B0u, 1);
    /* A white disc. */
    clear(SP_DISC, 0xFFFFFFu);
    paint(SP_DISC, 16, 16, 0, DISC_R, 0, 0, 0xFFFFFFFFu, 0);
    /* The coin: its edge seen below, the coin, its face, a glint. */
    clear(SP_COIN, 0x5A3A00u);
    paint(SP_COIN, 17.5f, 18, 0, COIN_R, 0, 0, 0x5A3A00FFu, 0);
    paint(SP_COIN, 16, 16, 0, COIN_R, 0, 0, 0xE8A817FFu, 0);
    paint(SP_COIN, 16 - COIN_R * 0.12f, 16 - COIN_R * 0.12f, 0, COIN_R * 0.72f, 0, 0, UI_GOLD, 0);
    paint(SP_COIN, 16 - COIN_R * 0.35f, 16 - COIN_R * 0.35f, 0, COIN_R * 0.2f, 0, 0, 0xFFF4B0FFu, 0);
    /* The medallion. */
    clear(SP_MEDAL, 0xFFFFFFu);
    paint(SP_MEDAL, 32, 32, 0, 18, 0, 0, 0xFFFFFFFFu, 0);
    paint(SP_MEDAL, 32, 32, 0, 15, 0, 0, UI_GOLD, 0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS, ATLAS, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
    return 0;
}

/* ---- textured quads, a batch a shape ----------------------------------------- */

#define MAX_QUADS 24
static GLfloat pos[MAX_QUADS * 8], st[MAX_QUADS * 8];
static GLubyte col[MAX_QUADS * 16];
static int nquads;

/* A quad from (x0, y0) to (x1, y1) on screen, texels (u0, v0) to (u1, v1). */
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t rgba)
{
    static const int corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int k;
    if (nquads == MAX_QUADS || x1 <= x0 || y1 <= y0)
        return;
    for (k = 0; k < 4; k++) {
        int q = nquads * 4 + k;
        pos[q * 2] = corner[k][0] ? x1 : x0;
        pos[q * 2 + 1] = corner[k][1] ? y1 : y0;
        st[q * 2] = (corner[k][0] ? u1 : u0) / ATLAS;
        st[q * 2 + 1] = (corner[k][1] ? v1 : v0) / ATLAS;
        col[q * 4] = (GLubyte)(rgba >> 24);
        col[q * 4 + 1] = (GLubyte)(rgba >> 16);
        col[q * 4 + 2] = (GLubyte)(rgba >> 8);
        col[q * 4 + 3] = (GLubyte)rgba;
    }
    nquads++;
}

static void flush(void)
{
    if (!nquads)
        return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glTexCoordPointer(2, GL_FLOAT, 0, st);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    glDrawArrays(GL_QUADS, 0, nquads * 4);
    dgk_gfx_tris += (uint32_t)nquads * 2;
    dgk_gfx_draws++;
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    nquads = 0;
}

/* Sprite s centred on (x, y), `scale` times its size, tinted rgba. */
static void sprite_at(int s, float x, float y, float scale, uint32_t rgba)
{
    const sprite *p = &sprites[s];
    float half = p->size * p->px * scale * 0.5f;
    quad(x - half, y - half, x + half, y + half, (float)p->x, (float)p->y, (float)(p->x + p->size),
         (float)(p->y + p->size), rgba);
}

/* A rounded rectangle into the batch: its corners quarters of the white
 * disc, the rest solid (the disc's middle); with r half its height, a pill
 * (two half discs and a middle: 3 quads, not 7). */
static void rounded(float x, float y, float w, float h, float r, uint32_t rgba)
{
    const float cu = 16.0f, cv = 128.0f + 16.0f, lo = 16.0f - DISC_R, hi = 16.0f + DISC_R;
    r = DGK_MIN(r, DGK_MIN(w, h) * 0.5f);
    if (r * 2 >= h - 0.01f) {
        quad(x, y, x + r, y + h, lo, cv - DISC_R, cu, cv + DISC_R, rgba);
        quad(x + r, y, x + w - r, y + h, cu, cv, cu, cv, rgba);
        quad(x + w - r, y, x + w, y + h, cu, cv - DISC_R, hi, cv + DISC_R, rgba);
        return;
    }
    quad(x, y, x + r, y + r, lo, cv - DISC_R, cu, cv, rgba);
    quad(x + w - r, y, x + w, y + r, cu, cv - DISC_R, hi, cv, rgba);
    quad(x, y + h - r, x + r, y + h, lo, cv, cu, cv + DISC_R, rgba);
    quad(x + w - r, y + h - r, x + w, y + h, cu, cv, hi, cv + DISC_R, rgba);
    quad(x + r, y, x + w - r, y + h, cu, cv, cu, cv, rgba);
    quad(x, y + r, x + r, y + h - r, cu, cv, cu, cv, rgba);
    quad(x + w - r, y + r, x + w, y + h - r, cu, cv, cu, cv, rgba);
}

void ui_plate(float x, float y, float w, float h, float r, uint32_t fill, uint32_t rim)
{
    if (rim)
        rounded(x - 2, y - 2, w + 4, h + 4, r + 2, rim);
    rounded(x, y, w, h, r, fill);
    flush();
}

void ui_panel(float x, float y, float w, float h, float r, uint32_t fill, uint32_t rim)
{
    rounded(x + 3, y + 4, w, h, r, 0x00000050u);      /* the shadow */
    ui_plate(x, y, w, h, r, fill, rim);
}

void ui_bar(float x, float y, float w, float h, float frac, uint32_t rgba)
{
    rounded(x, y, w, h, h * 0.5f, 0x0000006Cu);
    frac = DGK_CLAMP(frac, 0.0f, 1.0f);
    if (frac * w >= 1.0f)
        rounded(x, y, DGK_MAX(frac * w, h), h, h * 0.5f, rgba);
    flush();
}

void ui_disc(float x, float y, float r, uint32_t rgba)
{
    sprite_at(SP_DISC, x, y, r / DISC_R, rgba);
    flush();
}

void ui_coin(float x, float y, float r)
{
    sprite_at(SP_COIN, x, y, r / COIN_R, 0xFFFFFFFFu);
    flush();
}

void ui_dial(float x, float y)
{
    sprite_at(SP_DIAL, x, y, 1.0f, 0xFFFFFFFFu);
    flush();
}

void ui_medal(float x, float y)
{
    sprite_at(SP_MEDAL, x, y, 1.0f, 0xFFFFFFFFu);
    flush();
}

void ui_map_frame(float x, float y)
{
    sprite_at(SP_MAP, x, y, 1.0f, 0xFFFFFFFFu);
    flush();
}

/* ---- geometry: what moves --------------------------------------------------- */

static void colour(uint32_t rgba)
{
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
}

void ui_arc(float x, float y, float r0, float r1, float a0, float a1, uint32_t rgba, int n)
{
    int i;
    colour(rgba);
    glBegin(GL_QUAD_STRIP);
    dgk_gfx_draws++;
    for (i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        glVertex2f(x + r0 * cosf(a), y - r0 * sinf(a));
        glVertex2f(x + r1 * cosf(a), y - r1 * sinf(a));
    }
    glEnd();
    dgk_gfx_tris += (uint32_t)n * 2;
}

void ui_needle(float x, float y, float a, float len, uint32_t rgba)
{
    float c = cosf(a), s = -sinf(a), px = -s * 3.0f, py = c * 3.0f;
    colour(rgba);
    glBegin(GL_TRIANGLES);
    dgk_gfx_draws++;
    glVertex2f(x + px, y + py);
    glVertex2f(x + c * len, y + s * len);
    glVertex2f(x - px, y - py);
    glEnd();
    dgk_gfx_tris += 1;
}
