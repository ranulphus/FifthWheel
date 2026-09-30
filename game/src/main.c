/* Fifth Wheel - F1: drive the lorry round the test yard.
 *
 *   FWHEEL [kit options, see dgk/app.h] [-world FILE] [-autopilot [-laps N]]
 *          [-record FILE | -replay FILE] [-hash]
 *   FWHEEL -timedemo FILE -dbtest NAME   a replay as fast as it will go, timed
 *                                        as DOSBench test NAME (RESULTS.TXT)
 *   FWHEEL -probe [quick]                the performance probe (probe.h)
 *   FWHEEL -tourshots N                  N frames with the rig placed along the tour and at
 *                                        two depots, each saved as P<n> (DOS against Mesa)
 *
 * Keys: arrows or WASD (steer, accelerate, brake; hold the brake at a stop
 * to reverse), Space the handbrake, Esc to quit. */
#include "dgk/dgk.h"
#include "dgk/bench.h"
#include "dgk/replay.h"
#include "autopilot.h"
#include "probe.h"
#include "camera.h"
#include "lorry.h"
#include "sound.h"
#include "vehicle.h"
#include "world.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const dgk_font_data fw_font;

enum { SC_A = 4, SC_D = 7, SC_S = 22, SC_W = 26 };

/* One tick of input, as recorded. */
typedef struct input_frame {
    int8_t steer;               /* -127..127 */
    uint8_t accel, decel, buttons;
} input_frame;

typedef struct game {
    const char *world_path, *record_path, *replay_path, *dbtest;
    int use_autopilot, laps_wanted, hash, desync, last_laps, tourshots;
    world w;
    rig r, r_prev;
    camera cam, cam_prev;
    autopilot ap;
    lorry_meshes lorry;
    dgk_font font;
    dgk_replay *replay;
    float steer, accel, decel;  /* keyboard, smoothed */
    uint32_t jackknife_shown;   /* tick of the last jackknife callout */
    int jackknifes_seen;
    uint64_t fps_t0;
    uint32_t fps_frames;
    float fps;
} game;

static float approach(float v, float target, float rate)
{
    return v < target ? DGK_MIN(target, v + rate) : DGK_MAX(target, v - rate);
}

/* The keyboard, smoothed so that digital keys steer like a wheel. */
static void keyboard(game *g, input_frame *f)
{
    const float dt = 1.0f / DGK_TICK_HZ;
    int left = dgk_app.key_down[DGK_KEY_LEFT] || dgk_app.key_down[SC_A];
    int right = dgk_app.key_down[DGK_KEY_RIGHT] || dgk_app.key_down[SC_D];
    int up = dgk_app.key_down[DGK_KEY_UP] || dgk_app.key_down[SC_W];
    int down = dgk_app.key_down[DGK_KEY_DOWN] || dgk_app.key_down[SC_S];
    float target = (float)(left - right);
    g->steer = approach(g->steer, target, (target == 0 ? 3.5f : 2.5f) * dt);
    g->accel = approach(g->accel, up ? 1.0f : 0.0f, 4.0f * dt);
    g->decel = approach(g->decel, down ? 1.0f : 0.0f, 5.0f * dt);
    f->steer = (int8_t)(g->steer * 127.0f);
    f->accel = (uint8_t)(g->accel * 255.0f);
    f->decel = (uint8_t)(g->decel * 255.0f);
    f->buttons = (uint8_t)(dgk_app.key_down[DGK_KEY_SPACE] ? 1 : 0);
}

static int init(void *u)
{
    game *g = (game *)u;
    if (world_load(&g->w, g->world_path) != 0)
        return -1;
    rig_init(&g->r, g->w.spawn_x, g->w.spawn_y, g->w.spawn_heading);
    g->r_prev = g->r;
    camera_reset(&g->cam, &g->r);
    g->cam_prev = g->cam;
    autopilot_reset(&g->ap, &g->r, &g->w);
    lorry_build(&g->lorry);
    if (dgk_font_load(&g->font, &fw_font) != 0)
        return -1;
    if (g->replay_path) {
        g->replay = dgk_replay_play(g->replay_path, (int)sizeof(input_frame));
        if (!g->replay)
            return -1;
    } else if (g->record_path)
        g->replay = dgk_replay_record((int)sizeof(input_frame), 0);
    sound_init();
    if (g->dbtest) {
        dgk_bench_run("FWHEEL", "F2");
        dgk_app_bench_start(g->dbtest, 30);
    }
    glClearColor(0.55f, 0.78f, 0.95f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    g->fps_t0 = dgk_now_us();
    return 0;
}

/* -tourshots: the rig at pose k, no simulation: tour points evenly spread,
 * then depot 0's waiting trailer spot and depot 1's first bay. */
static void pose(game *g, int k)
{
    float x, y, heading;
    int n = g->tourshots - 2;
    if (k < n || !g->w.ndepots) {
        int i = (int)((long)k * g->w.npath / (n > 0 ? n : 1)) % g->w.npath, j = (i + 1) % g->w.npath;
        x = g->w.path[i * 2];
        y = g->w.path[i * 2 + 1];
        heading = atan2f(g->w.path[j * 2 + 1] - y, g->w.path[j * 2] - x);
    } else if (k == n) {
        x = g->w.depots[0].pickup[0];
        y = g->w.depots[0].pickup[1];
        heading = g->w.depots[0].pickup[2];
    } else {
        const wg_depot *d = &g->w.depots[g->w.ndepots > 1 ? 1 : 0];
        x = d->bay[0][0] + 12.0f * cosf(d->bay[0][2]);       /* the tractor ahead of a docked trailer */
        y = d->bay[0][1] + 12.0f * sinf(d->bay[0][2]);
        heading = d->bay[0][2];
    }
    rig_init(&g->r, x, y, heading);
    g->r_prev = g->r;
    camera_reset(&g->cam, &g->r);
    g->cam_prev = g->cam;
}

static void tick(void *u)
{
    game *g = (game *)u;
    input_frame f;
    rig_input in;
    uint32_t h;
    if (g->tourshots) {                              /* pose k is drawn and saved in frame k + 1 */
        if ((int)dgk_app.ticks >= g->tourshots) {
            dgk_app.quit = 1;
            return;
        }
        pose(g, (int)dgk_app.ticks);
        return;
    }
    if (dgk_app.key_pressed[DGK_KEY_ESCAPE])
        dgk_app.quit = 1;
    if (g->replay_path) {
        if (!dgk_replay_frame(g->replay, &f)) {
            dgk_log("FW-REPLAY end at tick %lu", (unsigned long)dgk_app.ticks);
            dgk_app.quit = 1;
            return;
        }
    } else if (g->use_autopilot) {
        rig_input a;
        autopilot_drive(&g->ap, &g->r, &g->w, &a);
        f.steer = (int8_t)(a.steer * 127.0f);
        f.accel = (uint8_t)(a.accel * 255.0f);
        f.decel = (uint8_t)(a.decel * 255.0f);
        f.buttons = 0;
    } else
        keyboard(g, &f);
    if (g->record_path && !g->replay_path)
        dgk_replay_frame(g->replay, &f);
    in.steer = f.steer / 127.0f;
    in.accel = f.accel / 255.0f;
    in.decel = f.decel / 255.0f;
    in.handbrake = f.buttons & 1;
    g->r_prev = g->r;
    g->cam_prev = g->cam;
    rig_step(&g->r, &in, 1.0f / DGK_TICK_HZ, &g->w);
    camera_tick(&g->cam, &g->r, 1.0f / DGK_TICK_HZ);
    sound_tick(&g->r);
    if (g->use_autopilot && g->replay_path) {
        rig_input unused;
        autopilot_drive(&g->ap, &g->r, &g->w, &unused);      /* keeps counting laps */
    }
    if (g->r.jackknifes != g->jackknifes_seen) {
        g->jackknifes_seen = g->r.jackknifes;
        g->jackknife_shown = dgk_app.ticks;
        dgk_log("FW-EVENT jackknife tick=%lu speed=%.1f", (unsigned long)dgk_app.ticks, g->r.v);
    }
    h = rig_hash(&g->r, 2166136261u);
    if (g->replay && !dgk_replay_hash(g->replay, dgk_app.ticks, h))
        g->desync = 1;
    if (g->hash && (dgk_app.ticks + 1) % 60 == 0)
        dgk_log("FW-HASH t=%lu h=%08lx x=%.2f y=%.2f v=%.2f gear=%d", (unsigned long)dgk_app.ticks + 1,
                (unsigned long)h, g->r.x, g->r.y, g->r.v, g->r.gear);
    if (g->use_autopilot) {
        if (g->ap.laps != g->last_laps) {
            g->last_laps = g->ap.laps;
            dgk_log("FW-LAP %d tick=%lu damage=%.1f hits=%d", g->ap.laps, (unsigned long)dgk_app.ticks,
                    g->r.damage, g->r.hits);
        }
        if (g->laps_wanted && g->ap.laps >= g->laps_wanted)
            dgk_app.quit = 1;
    }
}

static float lerp_angle(float a, float b, float t)
{
    float d = b - a;
    while (d > 3.14159265f)
        d -= 6.2831853f;
    while (d < -3.14159265f)
        d += 6.2831853f;
    return a + d * t;
}

static void bar(float x, float y, float w, float h, uint32_t rgba)
{
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void hud(game *g)
{
    char line[64], gbuf[12];
    const char *gear = gbuf;
    uint64_t now;
    if (g->r.gear < 0)
        gear = "R";
    else
        snprintf(gbuf, sizeof gbuf, "%d", g->r.gear);
    dgk_gfx_overlay_begin();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bar(18, 424, 204, 40, 0x00000080u);
    glDisable(GL_BLEND);
    bar(22, 452, 196 * DGK_CLAMP((g->r.rpm - 600.0f) / 1600.0f, 0.0f, 1.0f), 8,
        g->r.rpm > 1900 ? 0xFF5030FFu : 0x9FFFB0FFu);
    snprintf(line, sizeof line, "%3.0f KM/H  GEAR %s", fabsf(g->r.v) * 3.6f, gear);
    dgk_text(&g->font, 24, 428, 1.0f, 0xFFFFFFFFu, line);
    snprintf(line, sizeof line, "DAMAGE %3.0f%%", g->r.damage);
    dgk_text(&g->font, 470, 440, 1.0f, g->r.damage > 50 ? 0xFF6040FFu : 0xFFFFFFFFu, line);
    if (g->use_autopilot) {
        snprintf(line, sizeof line, "AUTOPILOT  LAP %d", g->ap.laps + 1);
        dgk_text(&g->font, 20, 16, 1.0f, 0xFFD23FFFu, line);
    }
    if (g->jackknife_shown && dgk_app.ticks - g->jackknife_shown < 90) {
        float t = (dgk_app.ticks - g->jackknife_shown) / 90.0f, s = 2.0f + 0.6f * sinf(t * 18.0f) * (1 - t);
        dgk_text(&g->font, 320 - dgk_text_width(&g->font, s, "JACKKNIFE!") / 2, 180, s, 0xFF5030FFu, "JACKKNIFE!");
    }
    if (!dgk_app.fixed) {
        now = dgk_now_us();
        if (++g->fps_frames >= 30 || now - g->fps_t0 > 1000000) {
            g->fps = g->fps_frames * 1e6f / (float)(now - g->fps_t0 ? now - g->fps_t0 : 1);
            g->fps_frames = 0;
            g->fps_t0 = now;
        }
        snprintf(line, sizeof line, "%.1f FPS", g->fps);
        dgk_text(&g->font, 540, 16, 1.0f, 0x9FFFB0FFu, line);
    } else {
        snprintf(line, sizeof line, "TICK %lu", (unsigned long)dgk_app.ticks);
        dgk_text(&g->font, 500, 16, 1.0f, 0x9FFFB0FFu, line);
    }
    dgk_gfx_overlay_end();
}

static void draw(void *u, float alpha)
{
    game *g = (game *)u;
    rig r = g->r;
    r.x = g->r_prev.x + (g->r.x - g->r_prev.x) * alpha;
    r.y = g->r_prev.y + (g->r.y - g->r_prev.y) * alpha;
    r.heading = lerp_angle(g->r_prev.heading, g->r.heading, alpha);
    r.trailer_heading = lerp_angle(g->r_prev.trailer_heading, g->r.trailer_heading, alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    {
        float tx = g->cam_prev.tx + (g->cam.tx - g->cam_prev.tx) * alpha;
        float ty = g->cam_prev.ty + (g->cam.ty - g->cam_prev.ty) * alpha;
        camera_apply(&g->cam, &g->cam_prev, alpha, (float)dgk_app.width / dgk_app.height, world_height(&g->w, tx, ty));
        world_draw(&g->w, tx, ty, 90.0f);
    }
    lorry_draw(&g->lorry, &r, &g->w);
    if (g->tourshots) {
        char name[16];
        snprintf(name, sizeof name, "P%d", (int)dgk_app.ticks - 1);
        if (dgk_app.ticks > 0 && (int)dgk_app.ticks <= g->tourshots)
            dgk_test_snapshot(name);                    /* the world and the lorry, no HUD */
        return;
    }
    hud(g);
}

static void quit(void *u)
{
    game *g = (game *)u;
    sound_stop();
    if (g->record_path && !g->replay_path) {
        if (dgk_replay_save(g->replay, g->record_path) == 0)
            dgk_log("FW-RECORD %s ticks=%lu", g->record_path, (unsigned long)dgk_app.ticks);
        else
            dgk_log("FW-ERROR cannot write %s", g->record_path);
    }
    dgk_log("FW-RESULT ticks=%lu laps=%d damage=%.1f hits=%d jackknifes=%d desync=%d x=%.2f y=%.2f",
            (unsigned long)dgk_app.ticks, g->ap.laps, g->r.damage, g->r.hits, g->r.jackknifes, g->desync, g->r.x,
            g->r.y);
    if (g->laps_wanted)
        dgk_test_check("laps", g->ap.laps >= g->laps_wanted, "%d of %d, damage %.1f, %d hits", g->ap.laps,
                       g->laps_wanted, g->r.damage, g->r.hits);
    if (g->replay_path)
        dgk_test_check("replay", !g->desync, "%s", g->desync ? "went elsewhere" : "the recorded state throughout");
    if (g->dbtest) {
        char notes[64];
        snprintf(notes, sizeof notes, "drive=%s ticks=%lu", g->replay_path ? "replay" : g->use_autopilot ? "autopilot" : "keys",
                 (unsigned long)dgk_app.ticks);
        dgk_app_bench_stop(g->desync ? "fail" : "ok", notes);
    }
    dgk_replay_free(g->replay);
    world_free(&g->w);
}

static int probe_quick;

static int init_probe(void *u)
{
    DGK_UNUSED(u);
    return probe_init(probe_quick);
}

static void draw_probe(void *u, float alpha)
{
    DGK_UNUSED(u);
    DGK_UNUSED(alpha);
    probe_draw();
}

int main(int argc, char **argv)
{
    static game g;
    static const dgk_app_desc desc = { "Fifth Wheel", init, tick, draw, quit };
    static const dgk_app_desc probe = { "Fifth Wheel probe", init_probe, NULL, draw_probe, NULL };
    int i;
#ifdef DGK_DOS
    g.world_path = "WORLD.PAK";
#else
    g.world_path = "build/data/WORLD.PAK";
#endif
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-autopilot"))
            g.use_autopilot = 1;
        else if (!strcmp(a, "-hash"))
            g.hash = 1;
        else if (!strcmp(a, "-laps") && v)
            g.laps_wanted = atoi(argv[++i]);
        else if (!strcmp(a, "-world") && v)
            g.world_path = argv[++i];
        else if (!strcmp(a, "-record") && v)
            g.record_path = argv[++i];
        else if (!strcmp(a, "-replay") && v)
            g.replay_path = argv[++i];
        else if (!strcmp(a, "-timedemo") && v)
            g.replay_path = argv[++i];
        else if (!strcmp(a, "-dbtest") && v)
            g.dbtest = argv[++i];
        else if (!strcmp(a, "-tourshots") && v)
            g.tourshots = atoi(argv[++i]);
        else if (!strcmp(a, "-probe")) {
            probe_quick = v && !strcmp(v, "quick");
            return dgk_app_run(&probe, NULL, argc, argv);
        }
    }
    return dgk_app_run(&desc, &g, argc, argv);
}
