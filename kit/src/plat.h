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

enum { PLAT_EV_NONE, PLAT_EV_QUIT, PLAT_EV_KEY_DOWN, PLAT_EV_KEY_UP, PLAT_EV_JOY_ADDED, PLAT_EV_JOY_AXIS,
       PLAT_EV_JOY_DOWN, PLAT_EV_JOY_UP };

typedef struct plat_event {
    int type;
    int key;                                /* SDL scancode; a joystick's axis or button */
    int value;                              /* an axis: -32768..32767 */
    const char *name;                       /* PLAT_EV_JOY_ADDED */
} plat_event;

int      plat_open(const plat_config *c, int *width, int *height);   /* 0 on success */
void     plat_close(void);
void     plat_pump(void);                   /* platform events and threads get a turn */
int      plat_next_event(plat_event *e);    /* 0 when none are left */
void     plat_swap(void);
uint64_t plat_now_us(void);
int      plat_snapshot(const char *name);   /* the frame being drawn (before the swap) */
/* The audio device: opened once the game has loaded (a long load would
 * leave the Sound Blaster's ring to run dry), closed before it quits. */
void     plat_audio_open(void);
void     plat_audio_close(void);
/* The Sound Blaster driver's count of chunks played as silence because the
 * ring was empty (DOSGL's SDL patch 0005), once the device has closed: 1 if
 * it reported, 0 if not (no SB, not DOS). */
int      plat_audio_underruns(int *underruns, int *chunks);
void     plat_audio_lock(void);
void     plat_audio_unlock(void);
const char *plat_describe(void);            /* renderer and drivers, for the log */
int      plat_modes(int (*wh)[2], int max);  /* the screen sizes on offer, smallest first */
void     plat_vsync(int on);

#endif
