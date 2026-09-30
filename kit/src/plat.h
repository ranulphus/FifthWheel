/* plat.h - what the kit needs from a platform (plat_sdl.c: DOS and Linux;
 * plat_headless.c: OSMesa for tests). Not part of the kit's API. */
#ifndef DGK_PLAT_H
#define DGK_PLAT_H

#include "dgk/base.h"

typedef struct plat_config {
    const char *title;
    int width, height, vsync, sound;
    uint32_t audio_rate;
} plat_config;

enum { PLAT_EV_NONE, PLAT_EV_QUIT, PLAT_EV_KEY_DOWN, PLAT_EV_KEY_UP };

typedef struct plat_event {
    int type;
    int key;                                /* SDL scancode */
} plat_event;

int      plat_open(const plat_config *c, int *width, int *height);   /* 0 on success */
void     plat_close(void);
void     plat_pump(void);                   /* platform events and threads get a turn */
int      plat_next_event(plat_event *e);    /* 0 when none are left */
void     plat_swap(void);
uint64_t plat_now_us(void);
int      plat_snapshot(const char *name);   /* the frame being drawn (before the swap) */
void     plat_audio_lock(void);
void     plat_audio_unlock(void);
const char *plat_describe(void);            /* renderer and drivers, for the log */

#endif
