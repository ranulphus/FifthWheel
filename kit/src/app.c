/* app.c - the main loop (see dgk/app.h). */
#include "dgk/app.h"
#include "dgk/log.h"
#include "dgk/mix.h"
#include "dgk/test.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_US (1000000u / DGK_TICK_HZ)
#define MAX_SHOTS 16

dgk_app_state dgk_app;

void dgk_test_begin(const char *name);   /* test.c */
int  dgk_test_end(void);

static struct {
    int width, height, vsync, frames, test, sound;
    int nshots;
    struct { uint32_t frame; char name[16]; } shots[MAX_SHOTS];
} opt;
static uint64_t last_service_us;

static void usage(const char *a)
{
    dgk_log("FW-WARN unknown or incomplete option %s", a);
}

static void parse(int argc, char **argv)
{
    int i;
    opt.width = 640;
    opt.height = 480;
    opt.vsync = 1;
    opt.sound = 1;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-mode") && v && sscanf(v, "%dx%d", &opt.width, &opt.height) == 2)
            i++;
        else if (!strcmp(a, "-novsync"))
            opt.vsync = 0;
        else if (!strcmp(a, "-frames") && v)
            opt.frames = atoi(argv[++i]);
        else if (!strcmp(a, "-fixed"))
            dgk_app.fixed = 1;
        else if (!strcmp(a, "-test"))
            opt.test = 1;
        else if (!strcmp(a, "-nosound"))
            opt.sound = 0;
        else if (!strcmp(a, "-shot") && v && opt.nshots < MAX_SHOTS) {
            const char *colon = strchr(v, ':');
            if (colon && colon[1]) {
                opt.shots[opt.nshots].frame = (uint32_t)atoi(v);
                snprintf(opt.shots[opt.nshots].name, sizeof opt.shots[0].name, "%s", colon + 1);
                opt.nshots++;
            } else
                usage(a);
            i++;
        } else if (a[0] == '-' && strchr("mfs", a[1]) && !v)
            usage(a);
        /* anything else belongs to the game */
    }
}

void dgk_service(void)
{
    plat_pump();
    last_service_us = plat_now_us();
}

void dgk_service_if_due(void)
{
    if (plat_now_us() - last_service_us >= 8000)
        dgk_service();
}

uint64_t dgk_now_us(void)
{
    return plat_now_us();
}

static void events(void)
{
    plat_event e;
    while (plat_next_event(&e)) {
        if (e.type == PLAT_EV_QUIT)
            dgk_app.quit = 1;
        else if (e.key >= 0 && e.key < DGK_KEY_MAX) {
            if (e.type == PLAT_EV_KEY_DOWN) {
                dgk_app.key_down[e.key] = 1;
                dgk_app.key_pressed[e.key] = 1;
            } else if (e.type == PLAT_EV_KEY_UP)
                dgk_app.key_down[e.key] = 0;
        }
    }
}

static void tick(const dgk_app_desc *d, void *u)
{
    if (d->tick)
        d->tick(u);
    dgk_app.ticks++;
    memset(dgk_app.key_pressed, 0, sizeof dgk_app.key_pressed);
}

int dgk_app_run(const dgk_app_desc *d, void *u, int argc, char **argv)
{
    plat_config c;
    uint64_t prev, acc = 0;
    int s, failed = 0;

    parse(argc, argv);
    if (opt.test)
        dgk_test_begin(d->title);
    c.title = d->title;
    c.width = opt.width;
    c.height = opt.height;
    c.vsync = opt.vsync;
    c.sound = opt.sound;
    c.audio_rate = 22050;
    dgk_mix_init(c.audio_rate);
    if (plat_open(&c, &dgk_app.width, &dgk_app.height) != 0) {
        dgk_test_check("start", 0, "the platform did not open");
        return opt.test ? dgk_test_end() : 1;
    }
    dgk_log("FW-START %dx%d %s", dgk_app.width, dgk_app.height, plat_describe());
    if (d->init && d->init(u) != 0) {
        dgk_test_check("init", 0, "the game did not start");
        plat_close();
        return opt.test ? dgk_test_end() : 1;
    }
    prev = plat_now_us();
    while (!dgk_app.quit) {
        dgk_service();
        events();
        if (dgk_app.fixed)
            tick(d, u);
        else {
            uint64_t now = plat_now_us();
            int n = 0;
            acc += now - prev;
            prev = now;
            if (acc > 250000)
                acc = 250000;                 /* after a stall: slow down, don't spiral */
            while (acc >= TICK_US && n < 4) {
                tick(d, u);
                acc -= TICK_US;
                n++;
                dgk_service_if_due();
            }
            if (n == 4 && acc >= TICK_US)
                acc = 0;
        }
        if (d->draw)
            d->draw(u, dgk_app.fixed ? 1.0f : (float)acc / TICK_US);
        for (s = 0; s < opt.nshots; s++)
            if (opt.shots[s].frame == dgk_app.frame && dgk_test_snapshot(opt.shots[s].name) != 0)
                failed = 1;
        dgk_service();
        plat_swap();
        dgk_app.frame++;
        if (opt.frames && dgk_app.frame >= (uint32_t)opt.frames)
            dgk_app.quit = 1;
    }
    if (d->quit)
        d->quit(u);
    dgk_log("FW-EXIT frames=%lu ticks=%lu", (unsigned long)dgk_app.frame, (unsigned long)dgk_app.ticks);
    if (opt.test) {
        dgk_test_check("frames", !failed && (!opt.frames || dgk_app.frame == (uint32_t)opt.frames), "%lu drawn",
                       (unsigned long)dgk_app.frame);
        plat_close();
        return dgk_test_end();
    }
    plat_close();
    return 0;
}
