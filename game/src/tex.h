/* tex.h - the textures' look test (HIGH, a pack made with fwgen --uv): one
 * atlas of cartoon patterns (wgen.h's WG_TX_*), greyscale, tinted by the
 * world's vertex colours (GL_MODULATE). */
#ifndef FW_TEX_H
#define FW_TEX_H

#include "dgk/base.h"

/* Draws the atlas (and its smaller levels) and loads it; its texture name. */
unsigned int tex_atlas(void);

#endif
