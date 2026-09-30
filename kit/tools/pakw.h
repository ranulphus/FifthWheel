/* pakw.h - writing packs (host tools only; see dgk/pak.h). */
#ifndef DGK_PAKW_H
#define DGK_PAKW_H

#include "dgk/base.h"

typedef struct pakw pakw;

pakw *pakw_new(uint32_t seed, uint32_t generator_hash);
void  pakw_section(pakw *w, uint32_t fourcc, const void *data, uint32_t size);
int   pakw_write(pakw *w, const char *path);     /* frees w; 0 on success */

#endif
