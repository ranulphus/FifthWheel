/* text.c - bitmap text from a baked atlas (see dgk/text.h). */
#include "dgk/text.h"
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <stdlib.h>

#define MAX_CHARS 128

int dgk_font_load(dgk_font *f, const dgk_font_data *d)
{
    static const uint8_t rgba[4][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 112 }, { 0, 0, 0, 255 }, { 255, 255, 255, 255 } };
    uint8_t *px = (uint8_t *)malloc((size_t)d->atlas_w * d->atlas_h * 4);
    GLuint t;
    int i;
    if (!px)
        return -1;
    for (i = 0; i < d->atlas_w * d->atlas_h; i++) {
        const uint8_t *c = rgba[d->pixels[i] & 3];
        px[i * 4 + 0] = c[0];
        px[i * 4 + 1] = c[1];
        px[i * 4 + 2] = c[2];
        px[i * 4 + 3] = c[3];
    }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, d->atlas_w, d->atlas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(px);
    f->data = d;
    f->texture = t;
    return 0;
}

float dgk_text_width(const dgk_font *f, float scale, const char *s)
{
    float w = 0;
    for (; *s; s++)
        w += f->data->advance * scale;
    return w;
}

void dgk_text(const dgk_font *f, float x, float y, float scale, uint32_t rgba, const char *s)
{
    static GLfloat pos[MAX_CHARS * 8], st[MAX_CHARS * 8];
    static GLubyte col[MAX_CHARS * 16];
    const dgk_font_data *d = f->data;
    const int cols = d->atlas_w / d->cell_w;
    const float cw = d->cell_w * scale, ch = d->cell_h * scale;
    int n = 0, k;
    for (; *s && n < MAX_CHARS; s++, x += d->advance * scale) {
        int c = (unsigned char)*s, g;
        float u0, v0, u1, v1;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        g = c < 128 ? d->glyph[c] : -1;
        if (g < 0)
            continue;
        u0 = (float)(g % cols * d->cell_w) / d->atlas_w;
        v0 = (float)(g / cols * d->cell_h) / d->atlas_h;
        u1 = u0 + (float)d->cell_w / d->atlas_w;
        v1 = v0 + (float)d->cell_h / d->atlas_h;
        pos[n * 8 + 0] = x;      pos[n * 8 + 1] = y;      st[n * 8 + 0] = u0; st[n * 8 + 1] = v0;
        pos[n * 8 + 2] = x + cw; pos[n * 8 + 3] = y;      st[n * 8 + 2] = u1; st[n * 8 + 3] = v0;
        pos[n * 8 + 4] = x + cw; pos[n * 8 + 5] = y + ch; st[n * 8 + 4] = u1; st[n * 8 + 5] = v1;
        pos[n * 8 + 6] = x;      pos[n * 8 + 7] = y + ch; st[n * 8 + 6] = u0; st[n * 8 + 7] = v1;
        for (k = 0; k < 4; k++) {
            col[n * 16 + k * 4 + 0] = (GLubyte)(rgba >> 24);
            col[n * 16 + k * 4 + 1] = (GLubyte)(rgba >> 16);
            col[n * 16 + k * 4 + 2] = (GLubyte)(rgba >> 8);
            col[n * 16 + k * 4 + 3] = (GLubyte)rgba;
        }
        n++;
    }
    if (!n)
        return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, f->texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glTexCoordPointer(2, GL_FLOAT, 0, st);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    glDrawArrays(GL_QUADS, 0, n * 4);
    dgk_gfx_tris += (uint32_t)n * 2;
    dgk_gfx_draws++;
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}
