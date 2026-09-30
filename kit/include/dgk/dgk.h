/* dgk - the DOS-GL Kit: the game-agnostic layer under Fifth Wheel.
 *
 * SDL3 opens the window and runs input, sound and time; drawing is OpenGL
 * 1.1 within DOS-GL's subset (compiled against DOS-GL's own <GL/gl.h> on
 * every target). One source builds for DOS (DJGPP: SDL3 with the DOS-GL
 * bridge), Linux (SDL3, desktop GL) and headless tests (OSMesa, a virtual
 * clock). Nothing allocates per frame. */
#ifndef DGK_DGK_H
#define DGK_DGK_H

#include "dgk/base.h"
#include "dgk/log.h"
#include "dgk/app.h"
#include "dgk/gfx.h"
#include "dgk/text.h"
#include "dgk/mix.h"
#include "dgk/test.h"

#endif
