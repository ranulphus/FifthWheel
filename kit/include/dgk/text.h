/* dgk/text.h - bitmap text from a baked atlas (kit/tools/fontbake.py): white
 * ink, a black outline and a drop shadow, tinted by the colour given, drawn
 * in the overlay (see dgk/gfx.h). */
#ifndef DGK_TEXT_H
#define DGK_TEXT_H

#include "dgk/base.h"

typedef struct dgk_font_data {
    int atlas_w, atlas_h;          /* texture size (powers of two) */
    int cell_w, cell_h;            /* one glyph's cell in the atlas */
    int advance;                   /* pen step per character, in atlas pixels */
    const uint8_t *pixels;         /* atlas_w * atlas_h: 0 clear, 1 shadow, 2 outline, 3 ink */
    signed char glyph[128];        /* character -> cell index, -1 for none */
} dgk_font_data;

typedef struct dgk_font {
    const dgk_font_data *data;
    unsigned int texture;
} dgk_font;

int   dgk_font_load(dgk_font *f, const dgk_font_data *d);   /* uploads the atlas */
void  dgk_text(const dgk_font *f, float x, float y, float scale, uint32_t rgba, const char *s);
float dgk_text_width(const dgk_font *f, float scale, const char *s);

#endif
