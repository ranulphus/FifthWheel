/* ui.c - the HUD's and menus' shapes (see ui.h). */
#include "ui.h"
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <math.h>

#define UI_PI 3.14159265f

static void colour(uint32_t rgba)
{
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
}

static void blend_on(void)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* A rounded rectangle as a fan from its middle: four quarter-circles of 4 steps. */
static void rounded(float x, float y, float w, float h, float r, uint32_t rgba)
{
    static const float cx[4] = { 1, 0, 0, 1 }, cy[4] = { 0, 0, 1, 1 };
    int c, i;
    r = DGK_MIN(r, DGK_MIN(w, h) * 0.5f);
    colour(rgba);
    glBegin(GL_TRIANGLE_FAN);
    dgk_gfx_draws++;
    glVertex2f(x + w * 0.5f, y + h * 0.5f);
    for (c = 0; c < 4; c++) {                         /* corners: top right, top left, bottom left, bottom right */
        float ox = cx[c] ? x + w - r : x + r, oy = cy[c] ? y + h - r : y + r;
        for (i = 0; i <= 4; i++) {
            float a = (c * 90.0f + i * 22.5f) * UI_PI / 180.0f;
            glVertex2f(ox + r * cosf(a), oy - r * sinf(a));
        }
    }
    glVertex2f(x + w - r + r, y + r);                 /* closes on the first corner's start */
    glEnd();
}

void ui_panel(float x, float y, float w, float h, float r, uint32_t fill, uint32_t rim)
{
    blend_on();
    rounded(x + 3, y + 4, w, h, r, 0x00000050u);      /* the shadow */
    if (rim)
        rounded(x - 2, y - 2, w + 4, h + 4, r + 2, rim);
    rounded(x, y, w, h, r, fill);
    glDisable(GL_BLEND);
}

void ui_bar(float x, float y, float w, float h, float frac, uint32_t rgba)
{
    blend_on();
    rounded(x, y, w, h, h * 0.5f, 0x0000006Cu);
    glDisable(GL_BLEND);
    frac = DGK_CLAMP(frac, 0.0f, 1.0f);
    if (frac * w >= 1.0f)
        rounded(x, y, DGK_MAX(frac * w, h), h, h * 0.5f, rgba);
}

void ui_disc(float x, float y, float r, uint32_t rgba, int n)
{
    int i;
    colour(rgba);
    glBegin(GL_TRIANGLE_FAN);
    dgk_gfx_draws++;
    glVertex2f(x, y);
    for (i = 0; i <= n; i++)
        glVertex2f(x + r * cosf(i * 2 * UI_PI / n), y - r * sinf(i * 2 * UI_PI / n));
    glEnd();
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
}

void ui_ring(float x, float y, float r0, float r1, uint32_t rgba, int n)
{
    ui_arc(x, y, r0, r1, 0, 2 * UI_PI, rgba, n);
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
}

void ui_coin(float x, float y, float r)
{
    ui_disc(x + 1.5f, y + 2, r, 0x5A3A00FFu, 16);    /* its edge, seen below */
    ui_disc(x, y, r, 0xE8A817FFu, 16);
    ui_disc(x - r * 0.12f, y - r * 0.12f, r * 0.72f, UI_GOLD, 16);
    ui_disc(x - r * 0.35f, y - r * 0.35f, r * 0.2f, 0xFFF4B0FFu, 8);   /* the glint */
}
