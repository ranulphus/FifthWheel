/* dgk/app.h - the main loop: fixed 60 Hz simulation ticks (at most four a
 * frame), then a draw with the fraction of a tick that has passed, then the
 * swap. SDL gets a turn (dgk_service) at the start of each frame, after each
 * tick and just before the swap: on DOS its threads switch only there, and
 * the Sound Blaster's ring holds about 45 ms.
 *
 * Command line (all targets):
 *   -mode WxH       window / screen size (default 640x480)
 *   -novsync        swaps do not wait for the retrace
 *   -frames N       quit after N frames
 *   -fixed          one tick per frame (deterministic frames for tests)
 *   -shot F:NAME    save frame F as NAME (DOS: C:\OUT\NAME.PPM; else out/NAME.ppm)
 *   -test           report on COM1 with the HX- protocol (DOS; Loop A)
 *   -nosound        no audio device
 *   -nodraw         draw only the frames -shot saves (long simulation tests)
 *   -noexit         with -test: do not end the Loop A run at the end (a job
 *                   that runs more programs after this one)
 *
 * The first joystick (a DOS gameport stick or wheel through SDL) is read
 * into dgk_app.joy_*. Headless, DGK_JOY scripts one: a comma list of
 * TICK:axis:N:VALUE (-32768..32767) and TICK:button:N:0|1, each applied
 * at that tick (as Loop A's --keys joy items are in 86Box).
 */
#ifndef DGK_APP_H
#define DGK_APP_H

#include "dgk/base.h"

#define DGK_TICK_HZ 60

/* Keys the kit names (SDL scancodes underneath). */
enum {
    DGK_KEY_ESCAPE = 41, DGK_KEY_RETURN = 40, DGK_KEY_SPACE = 44,
    DGK_KEY_RIGHT = 79, DGK_KEY_LEFT = 80, DGK_KEY_DOWN = 81, DGK_KEY_UP = 82,
    DGK_KEY_MAX = 512
};

#define DGK_JOY_AXES 4
#define DGK_JOY_BUTTONS 8

typedef struct dgk_app_desc {
    const char *title;
    int (*init)(void *u);                 /* after the window and GL exist; 0 = go on */
    void (*tick)(void *u);                /* one simulation step of 1/DGK_TICK_HZ s */
    void (*draw)(void *u, float alpha);   /* alpha: 0..1 of a tick since the last one */
    void (*quit)(void *u);
} dgk_app_desc;

typedef struct dgk_app_state {
    int width, height;                    /* drawing size in pixels */
    uint32_t frame, ticks;                /* frames drawn, ticks run */
    uint8_t key_down[DGK_KEY_MAX];        /* held */
    uint8_t key_pressed[DGK_KEY_MAX];     /* went down since the last tick */
    int joy_present;                      /* a joystick is open */
    char joy_name[40];
    int16_t joy_axis[DGK_JOY_AXES];       /* -32768..32767 */
    uint8_t joy_down[DGK_JOY_BUTTONS];
    uint8_t joy_pressed[DGK_JOY_BUTTONS]; /* went down since the last tick */
    int quit;                             /* set to end the loop */
    int fixed;                            /* -fixed */
} dgk_app_state;

extern dgk_app_state dgk_app;

int  dgk_app_run(const dgk_app_desc *d, void *u, int argc, char **argv);
/* Time frames (swap to swap) and their triangles into a DOSBench test
 * (dgk/bench.h), from the next frame until stopped. */
void dgk_app_bench_start(const char *test, int warmup_frames);
void dgk_app_bench_stop(const char *status, const char *notes);
void dgk_service(void);                   /* give SDL (and its audio thread) a turn */
void dgk_service_if_due(void);            /* the same, if 8 ms have passed since the last */
uint64_t dgk_now_us(void);

#endif
