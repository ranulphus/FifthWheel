/* tex.h - HIGH's textures (with WORLDHI.PAK, wgen.h): one atlas of cartoon
 * patterns (WG_TX_*), greyscale, tinted by the world's vertex colours
 * (GL_MODULATE); anisotropic filtering where the card has it. */
#ifndef FW_TEX_H
#define FW_TEX_H

#include "dgk/base.h"

/* Draws the atlas (and its smaller levels) and loads it; its texture name. */
unsigned int tex_atlas(void);

#endif
