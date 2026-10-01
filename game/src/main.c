/* Fifth Wheel: deliveries with an articulated lorry.
 *
 *   FWHEEL [kit options, see dgk/app.h] [-world FILE] [-record FILE | -replay FILE] [-hash]
 *          [-trace N]                    the rig's pose every N ticks (FW-TRACE)
 *   FWHEEL -dockpose                     a trailer reversing into a bay, held still (pictures)
 *   FWHEEL -soundtest                    each sound in turn, then quit (the SOUND suite)
 *   FWHEEL -fxtest                       every flourish far past its cap for 2 s (HX-TEST fx-caps)
 *   FWHEEL -calibrate                    the joystick's calibration screen, saved, then quit
 *          [-joylog]                     the joystick's axes and mapped controls every 0.5 s (FW-JOY)
 *          [-career FILE] [-money N]     keep a career in FILE in a test run; start it with $N
 * Settings: FWHEEL.CFG beside the game (fwheel.cfg on Linux; FW_CFG overrides). The career:
 * CAREER.DAT (career.dat; FW_CAREER), kept when playing (not in tests, replays or recordings).
 *   FWHEEL -autopilot [-laps N]          round the world's tour with a trailer
 *   FWHEEL -autojob [-job D:T:B]         the autopilot does the shortest job on offer at
 *                                        depot 0, or the job from depot D to depot T's bay B
 *                                        (from 0; FW-JOB lines; the test: graded OK or better)
 *   FWHEEL -timedemo FILE -dbtest NAME   a replay as fast as it will go, timed
 *                                        as DOSBench test NAME (RESULTS.TXT)
 *   FWHEEL -probe [quick]                the performance probe (probe.h)
 *   FWHEEL -tourshots N                  N frames with the rig placed along the tour and at
 *                                        two depots, each saved as P<n> (DOS against Mesa)
 *
 * Keys: arrows or WASD (steer, accelerate, brake; hold the brake at a stop
 * to reverse), Space the handbrake, H the horn, 1-3 take a job from the
 * board, Backspace cancel it (before coupling), G the garage (stopped), J the joystick's set-up,
 * Esc to quit. A joystick or wheel: steering, accelerator and brake as
 * calibrated, button 1 the handbrake, button 2 the horn. */
#include "game.h"
#include "dgk/bench.h"
#include "guides.h"
#include "dgk/save.h"
#include "probe.h"
#include "sound.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const dgk_font_data fw_font;

enum { SC_A = 4, SC_D = 7, SC_G = 10, SC_H = 11, SC_J = 13, SC_S = 22, SC_W = 26, SC_1 = 30, SC_BACKSPACE = 42 };

/* One tick of input, as recorded. Buttons: bit 0 the handbrake, bits 1-2
 * a job taken from the board (1-3), bit 3 the job cancelled, bit 4 the
 * horn held. */
typedef struct input_frame {
    int8_t steer;               /* -127..127 */
    uint8_t accel, decel, buttons;
} input_frame;

static float approach(float v, float target, float rate)
{
    return v < target ? DGK_MIN(target, v + rate) : DGK_MAX(target, v - rate);
}

/* The keyboard, smoothed so that digital keys steer like a wheel, and the
 * joystick (input.h): the keys steer when held, the stick otherwise; the
 * pedals take the further of the two. */
static void controls(game *g, input_frame *f)
{
    const float dt = 1.0f / DGK_TICK_HZ;
    int left = dgk_app.key_down[DGK_KEY_LEFT] || dgk_app.key_down[SC_A];
    int right = dgk_app.key_down[DGK_KEY_RIGHT] || dgk_app.key_down[SC_D];
    int up = dgk_app.key_down[DGK_KEY_UP] || dgk_app.key_down[SC_W];
    int down = dgk_app.key_down[DGK_KEY_DOWN] || dgk_app.key_down[SC_S];
    float target = (float)(left - right), steer, accel, decel, js = 0, ja = 0, jb = 0;
    int i;
    g->steer = approach(g->steer, target, (target == 0 ? 3.5f : 2.5f) * dt);
    g->accel = approach(g->accel, up ? 1.0f : 0.0f, 4.0f * dt);
    g->decel = approach(g->decel, down ? 1.0f : 0.0f, 5.0f * dt);
    if (dgk_app.joy_present)
        joymap_read(&g->jmap, &js, &ja, &jb);
    steer = (left || right || js == 0) ? g->steer : js;
    accel = DGK_MAX(g->accel, ja);
    decel = DGK_MAX(g->decel, jb);
    f->steer = (int8_t)(steer * 127.0f);
    f->accel = (uint8_t)(accel * 255.0f);
    f->decel = (uint8_t)(decel * 255.0f);
    f->buttons = (uint8_t)(dgk_app.key_down[DGK_KEY_SPACE] || dgk_app.joy_down[g->jmap.handbrake_button] ? 1 : 0);
    for (i = 0; i < JOB_OFFERS; i++)
        if (dgk_app.key_pressed[SC_1 + i])
            f->buttons |= (uint8_t)((i + 1) << 1);
    if (dgk_app.key_pressed[SC_BACKSPACE])
        f->buttons |= 8;
    if (dgk_app.key_down[SC_H] || dgk_app.joy_down[g->jmap.horn_button])
        f->buttons |= 16;
}

/* The career: loaded at the start of play, saved after each delivery, each
 * purchase and at the end. Tests leave it alone unless given -career. */
static void career_save_now(game *g, const char *why)
{
    if (!g->careerful)
        return;
    g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
    if (dgk_save_write(g->career_path, CAREER_MAGIC, CAREER_VERSION, &g->car, sizeof g->car) == 0)
        dgk_log("FW-CAREER saved %s (%s): money %lu, %lu delivered", g->career_path, why,
                (unsigned long)g->car.money, (unsigned long)g->car.delivered);
    else
        dgk_log("FW-WARN cannot save %s", g->career_path);
}

/* The career's fitted items on the lorry and in the horn. */
static void career_style(game *g)
{
    lorry_style(&g->lorry, career_item(&g->car, KIND_PAINT)->value, (int)career_item(&g->car, KIND_DECAL)->value,
                (int)career_item(&g->car, KIND_CAB)->value);
    sound_set_horn((int)career_item(&g->car, KIND_HORN)->value);
}

static void career_start(game *g)
{
    int r;
    g->careerful = g->career_given || (g->mode == MODE_DRIVE && !dgk_test_active() && !g->replay_path &&
                                       !g->record_path && !g->tourshots && !g->dockpose && !g->soundtest &&
                                       !g->calibrate_only);
    if (!g->careerful)
        return;
    r = dgk_save_read(g->career_path, CAREER_MAGIC, CAREER_VERSION, &g->car, sizeof g->car);
    if (r == DGK_SAVE_OK)
        dgk_log("FW-CAREER loaded %s: money %lu, %lu delivered, paint %s, horn %s", g->career_path,
                (unsigned long)g->car.money, (unsigned long)g->car.delivered, career_item(&g->car, KIND_PAINT)->name,
                career_item(&g->car, KIND_HORN)->name);
    else {
        if (r == DGK_SAVE_BAD)
            dgk_log("FW-WARN %s is damaged or from another version: a new career", g->career_path);
        career_new(&g->car);
    }
    if (g->start_money >= 0)
        g->car.money = (uint32_t)g->start_money;
    g->jobs.money = (int)g->car.money;
    g->jobs.licences = career_licences(&g->car);
}

/* The calibration screen, while it is open: the world waits. Done, the
 * joystick's map goes into the settings file. */
static void calibration(game *g)
{
    joymap *m = &g->cal.result;
    int ok;
    if (dgk_app.key_pressed[DGK_KEY_ESCAPE]) {
        g->cal.step = CAL_OFF;
        dgk_log("FW-CAL cancelled");
        if (g->calibrate_only)
            dgk_app.quit = 1;
        return;
    }
    if (!calib_tick(&g->cal))
        return;
    g->jmap = *m;
    joymap_store(m, &g->cfg);
    ok = dgk_cfg_save(&g->cfg, g->cfg_path) == 0;
    dgk_log("FW-CAL saved %s %s: steer axis %d (%d, rest %d, %d), accel axis %d (rest %d, %d), brake axis %d "
            "(rest %d, %d)", g->cfg_path, ok ? "ok" : "FAILED", m->steer_axis, m->steer_left, m->steer_rest,
            m->steer_right, m->accel_axis, m->accel_rest, m->accel_full, m->brake_axis, m->brake_rest,
            m->brake_full);
    if (g->calibrate_only)
        dgk_test_check("calibrate", ok, "steer axis %d, accel axis %d, brake axis %d, saved to %s", m->steer_axis,
                       m->accel_axis, m->brake_axis, g->cfg_path);
    g->cal.step = CAL_OFF;
    if (g->calibrate_only)
        dgk_app.quit = 1;
}

/* -dockpose: a docking scene to look at (DOS against Mesa): a box trailer
 * reversing into depot 1's middle bay, 8 m out, a little off line, the
 * wheels turned (held by the tick's input). */
static void dock_pose(game *g)
{
    const float th_off = 4.0f * WG_PI / 180.0f, phi = 10.0f * WG_PI / 180.0f;
    float bx, by, bh, th, rx, ry, hx, hy;
    jobs *j = &g->jobs;
    jobs_set_offer(j, &g->w, 0, 0, 1, 1);
    jobs_take(j, &g->w, &g->r, 0);
    j->trailers[0].present = 0;
    j->state = JOB_DOCKING;
    jobs_bay(j, &g->w, &bx, &by, &bh);
    th = bh + th_off;                                  /* the trailer points out of the bay */
    rx = bx + cosf(bh) * 8.0f - sinf(bh) * 0.6f;       /* its back: 8 m out, 0.6 m across */
    ry = by + sinf(bh) * 8.0f + cosf(bh) * 0.6f;
    hx = rx + RIG_TRAILER_REAR * cosf(th);
    hy = ry + RIG_TRAILER_REAR * sinf(th);
    rig_init(&g->r, hx - RIG_HITCH * cosf(th + phi), hy - RIG_HITCH * sinf(th + phi), th + phi);
    rig_couple(&g->r, TRAILER_BOX, 16000.0f, th);
    g->r.gear = -1;
}

static int init(void *u)
{
    game *g = (game *)u;
    if (world_load(&g->w, g->world_path) != 0)
        return -1;
    career_new(&g->car);                             /* the starting items, whatever the mode */
    if (dgk_cfg_load(&g->cfg, g->cfg_path) == 0)
        dgk_log("FW-CFG loaded %s", g->cfg_path);
    joymap_load(&g->jmap, &g->cfg);
    if (g->jmap.calibrated)
        dgk_log("FW-CFG joystick: steer axis %d, accel axis %d, brake axis %d", g->jmap.steer_axis,
                g->jmap.accel_axis, g->jmap.brake_axis);
    if (g->calibrate_only)
        calib_start(&g->cal);
    rig_init(&g->r, g->w.spawn_x, g->w.spawn_y, g->w.spawn_heading);
    g->has_jobs = g->w.ndepots > 0 && g->mode != MODE_TOUR && !g->tourshots;
    if (g->has_jobs) {
        /* A lone tractor, lined up in front of depot 0's waiting trailer. */
        float x, y, heading;
        int from = 0;
        jobs_init(&g->jobs, &g->w, 1);
        career_start(g);
        if (g->job_to >= 0 && g->job_from < g->w.ndepots && g->job_to < g->w.ndepots && g->job_from != g->job_to) {
            from = g->job_from;
            jobs_set_offer(&g->jobs, &g->w, 0, from, g->job_to, g->job_bay % WG_BAYS);
            g->jobs.offers[1] = g->jobs.offers[2] = g->jobs.offers[0];
        }
        jobpilot_start(&g->jobs, from, &x, &y, &heading);
        rig_init(&g->r, x, y, heading);
        g->r.has_trailer = 0;
        if (g->mode == MODE_JOB) {                   /* taken in the first tick, as a key press (recorded) */
            int i, best = 0;
            for (i = 1; i < JOB_OFFERS; i++)
                if (g->jobs.offers[i].distance < g->jobs.offers[best].distance)
                    best = i;
            g->autotake = best + 1;
            jobpilot_reset(&g->jp, &g->jobs);
        }
        if (g->dockpose)
            dock_pose(g);
    } else if (g->mode == MODE_JOB) {
        dgk_log("FW-ERROR -autojob needs a world with depots");
        return -1;
    }
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
    career_style(g);
    fx_init(&g->fxs, g->jobs.money);
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

/* Coupling, delivery: sounds, callouts; the autojob's end. */
static void job_events(game *g)
{
    jobs *j = &g->jobs;
    jobs_tick(j, &g->r, &g->w, 1.0f / DGK_TICK_HZ);
    if (j->event == JOB_EVENT_COUPLED) {
        float hx, hy;
        sound_clunk();
        hud_callout(g, "COUPLED!", NULL, 0x9FFFB0FFu);
        rig_hitch(&g->r, &hx, &hy);
        fx_dust_at(&g->fxs, &g->w, hx, hy, 6, 2.5f);
        fx_bump(&g->fxs, -0.5f);
    } else if (j->event == JOB_EVENT_DELIVERED) {
        static const char *const cheer[] = { "", "DELIVERED", "GOOD PARK!", "GREAT PARK!", "PERFECT PARK!" };
        static const uint32_t cheer_rgba[] = { 0, 0xFFFFFFFFu, 0x9FFFB0FFu, 0x5FD8FFFFu, 0xFFD23FFFu };
        sound_chime(j->grade);
        fx_coins(&g->fxs, DGK_CLAMP(j->earned / 40, 4, FX_COINS));
        if (j->grade >= GRADE_GREAT)
            fx_confetti(&g->fxs, j->grade == GRADE_PERFECT ? 56 : 32);
        g->car.delivered++;
        g->car.perfect += j->grade == GRADE_PERFECT;
        g->car.metres += (uint32_t)j->current.distance;
        career_save_now(g, "delivery");
        snprintf(g->callout_buf, sizeof g->callout_buf, "+$%d", j->earned);
        hud_callout(g, cheer[j->grade], g->callout_buf, cheer_rgba[j->grade]);
        if (g->mode == MODE_JOB)
            g->done_tick = dgk_app.ticks;
    }
}

/* Coupling: the camera looks along the waiting trailer from beyond its
 * nose, at the gap between the fifth wheel and the kingpin. Docking: into
 * the bay, at the trailer's back (drawn on towards the dock). Otherwise it
 * follows (0). */
static int camera_focus_for(const game *g, camera_focus *f)
{
    const jobs *j = &g->jobs;
    if (j->state == JOB_TO_PICKUP) {
        const parked *t = &j->trailers[j->current.from];
        float hx, hy;
        rig_hitch(&g->r, &hx, &hy);
        if ((t->x - hx) * (t->x - hx) + (t->y - hy) * (t->y - hy) > 30.0f * 30.0f)
            return 0;
        f->x = (t->x + hx) * 0.5f;
        f->y = (t->y + hy) * 0.5f;
        f->yaw = t->heading + WG_PI;
        return 1;
    }
    if (j->state == JOB_DOCKING && j->gap < 45.0f) {
        float bx, by, bh, rx, ry, dx, dy, d;
        jobs_bay(j, &g->w, &bx, &by, &bh);
        rig_trailer_rear(&g->r, &rx, &ry);
        dx = bx - rx;
        dy = by - ry;
        d = sqrtf(dx * dx + dy * dy);
        d = d > 10.0f ? 10.0f / d : 1.0f;
        f->x = rx + dx * d * 0.6f;
        f->y = ry + dy * d * 0.6f;
        f->yaw = bh + WG_PI;
        return 1;
    }
    return 0;
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
    if (dgk_app.ticks == 0 && g->mode == MODE_DRIVE)
        dgk_log("FW-PLAY ready");                    /* scripted keys start from here */
    if (g->cal.step != CAL_OFF) {
        calibration(g);
        return;
    }
    if (g->gar.open) {                               /* the garage: the world waits */
        int changed;
        g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
        changed = garage_tick(&g->gar, &g->car, &g->lorry);
        g->jobs.money = (int)g->car.money;           /* what was spent, before the save copies it back */
        if (g->careerful)
            g->jobs.licences = career_licences(&g->car);
        if (changed)
            career_save_now(g, "garage");
        if (!g->gar.open)
            career_style(g);                         /* what is fitted, not what was tried on */
        return;
    }
    if (dgk_app.key_pressed[SC_G] && fabsf(g->r.v) < 0.5f && g->has_jobs && !g->replay_path && !g->record_path &&
        g->mode == MODE_DRIVE) {
        g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
        garage_open(&g->gar, &g->car, &g->lorry);
        return;
    }
    if (dgk_app.key_pressed[SC_J] && !g->replay_path && !g->record_path && g->mode == MODE_DRIVE) {
        calib_start(&g->cal);                        /* not while recording: the world waits meanwhile */
        return;
    }
    if (g->joylog && (dgk_app.ticks == 0 || (dgk_app.ticks + 1) % 30 == 0)) {
        float js = 0, ja = 0, jb = 0;
        joymap_read(&g->jmap, &js, &ja, &jb);
        if (dgk_app.ticks == 0)
            dgk_log("FW-JOYLOG ready");
        else
            dgk_log("FW-JOY axes %d %d %d %d buttons %d%d steer %.2f accel %.2f brake %.2f", dgk_app.joy_axis[0],
                    dgk_app.joy_axis[1], dgk_app.joy_axis[2], dgk_app.joy_axis[3], dgk_app.joy_down[0],
                    dgk_app.joy_down[1], js, ja, jb);
    }
    if (g->fxtest) {                                 /* every flourish, far past its cap */
        fx_coins(&g->fxs, 4);
        fx_confetti(&g->fxs, 8);
        fx_dust_at(&g->fxs, &g->w, g->r.x, g->r.y, 4, 6.0f);
        fx_tick(&g->fxs, &g->r, &g->w, g->jobs.money + (int)dgk_app.ticks * 10);
        if (dgk_app.ticks >= 120)
            dgk_app.quit = 1;
        return;
    }
    if (g->soundtest) {                              /* each sound in turn, nothing driven */
        if (!sound_test_tick(dgk_app.ticks))
            dgk_app.quit = 1;
        return;
    }
    if (dgk_app.key_pressed[DGK_KEY_ESCAPE])
        dgk_app.quit = 1;
    if (g->dockpose) {                               /* held still, the wheels turned */
        f.steer = 60;
        f.accel = f.decel = f.buttons = 0;
    } else if (g->replay_path) {
        if (!dgk_replay_frame(g->replay, &f)) {
            dgk_log("FW-REPLAY end at tick %lu", (unsigned long)dgk_app.ticks);
            dgk_app.quit = 1;
            return;
        }
    } else if (g->mode == MODE_TOUR || g->mode == MODE_JOB) {
        rig_input a;
        if (g->mode == MODE_TOUR)
            autopilot_drive(&g->ap, &g->r, &g->w, &a);
        else
            jobpilot_drive(&g->jp, &g->jobs, &g->r, &g->w, &a);
        f.steer = (int8_t)(a.steer * 127.0f);
        f.accel = (uint8_t)(a.accel * 255.0f);
        f.decel = (uint8_t)(a.decel * 255.0f);
        f.buttons = (uint8_t)(g->autotake << 1);
        g->autotake = 0;
    } else
        controls(g, &f);
    if (g->record_path && !g->replay_path)
        dgk_replay_frame(g->replay, &f);
    in.steer = f.steer / 127.0f;
    in.accel = f.accel / 255.0f;
    in.decel = f.decel / 255.0f;
    in.handbrake = f.buttons & 1;
    g->r_prev = g->r;
    g->cam_prev = g->cam;
    if (g->has_jobs) {
        if ((f.buttons >> 1) & 3)
            jobs_take(&g->jobs, &g->w, &g->r, ((f.buttons >> 1) & 3) - 1);
        if (f.buttons & 8)
            jobs_cancel(&g->jobs);
        jobs_props(&g->jobs, &g->w, g->r.x, g->r.y);
    }
    rig_step(&g->r, &in, 1.0f / DGK_TICK_HZ, &g->w);
    if (g->has_jobs)
        job_events(g);
    {
        camera_focus f;
        int focus = g->has_jobs && camera_focus_for(g, &f);
        camera_tick(&g->cam, &g->r, 1.0f / DGK_TICK_HZ, focus ? &f : NULL);
        g->rearcam = focus && g->r.gear < 0;          /* the reversing camera's look and guides */
    }
    sound_tick(&g->r, f.buttons & 16);
    fx_tick(&g->fxs, &g->r, &g->w, g->jobs.money);
    if (g->fxs.coins_landed)
        sound_coin();
    if (g->r.hits != g->hits_seen) {                 /* a knock: the cab jolts, dust flies */
        g->hits_seen = g->r.hits;
        fx_bump(&g->fxs, 0.9f);
        fx_dust_at(&g->fxs, &g->w, g->r.x + 4.5f * cosf(g->r.heading), g->r.y + 4.5f * sinf(g->r.heading), 3, 2.0f);
    }
    if (g->mode == MODE_TOUR && g->replay_path) {
        rig_input unused;
        autopilot_drive(&g->ap, &g->r, &g->w, &unused);      /* keeps counting laps */
    }
    if (g->r.jackknifes != g->jackknifes_seen) {
        g->jackknifes_seen = g->r.jackknifes;
        hud_callout(g, "JACKKNIFE!", NULL, 0xFF5030FFu);
        {
            float ax, ay;
            rig_trailer_axle(&g->r, &ax, &ay);
            fx_dust_at(&g->fxs, &g->w, ax, ay, 8, 3.0f);
            fx_bump(&g->fxs, 0.6f);
        }
        dgk_log("FW-EVENT jackknife tick=%lu speed=%.1f", (unsigned long)dgk_app.ticks, g->r.v);
    }
    h = rig_hash(&g->r, 2166136261u);
    if (g->has_jobs) {
        h = dgk_fnv1a(h, &g->jobs.state, sizeof g->jobs.state);
        h = dgk_fnv1a(h, &g->jobs.money, sizeof g->jobs.money);
    }
    if (g->replay && !dgk_replay_hash(g->replay, dgk_app.ticks, h))
        g->desync = 1;
    if (g->trace && (dgk_app.ticks + 1) % g->trace == 0) {
        float ax, ay;
        rig_trailer_axle(&g->r, &ax, &ay);
        dgk_log("FW-TRACE t=%lu x=%.2f y=%.2f h=%.3f th=%.3f v=%.2f ax=%.2f ay=%.2f hits=%d",
                (unsigned long)dgk_app.ticks + 1, g->r.x, g->r.y, g->r.heading, g->r.trailer_heading, g->r.v, ax, ay,
                g->r.hits);
    }
    if (g->hash && (dgk_app.ticks + 1) % 60 == 0)
        dgk_log("FW-HASH t=%lu h=%08lx x=%.2f y=%.2f v=%.2f gear=%d", (unsigned long)dgk_app.ticks + 1,
                (unsigned long)h, g->r.x, g->r.y, g->r.v, g->r.gear);
    if (g->mode == MODE_TOUR) {
        if (g->ap.laps != g->last_laps) {
            g->last_laps = g->ap.laps;
            dgk_log("FW-LAP %d tick=%lu damage=%.1f hits=%d", g->ap.laps, (unsigned long)dgk_app.ticks,
                    g->r.damage, g->r.hits);
        }
        if (g->laps_wanted && g->ap.laps >= g->laps_wanted)
            dgk_app.quit = 1;
    }
    if (g->mode == MODE_JOB && g->done_tick && dgk_app.ticks - g->done_tick >= 150)
        dgk_app.quit = 1;                            /* after the callout */
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

static void draw(void *u, float alpha)
{
    game *g = (game *)u;
    if (g->gar.open) {
        garage_draw(&g->gar, &g->car, &g->lorry, &g->font, alpha);
        return;
    }
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
    lorry_draw(&g->lorry, &r, &g->w, &g->fxs.sway);
    if (g->has_jobs) {
        int i;
        for (i = 0; i < g->w.ndepots; i++) {
            const parked *t = &g->jobs.trailers[i], *d = &g->jobs.docked[i];
            float dx = g->w.depots[i].x - r.x, dy = g->w.depots[i].y - r.y;
            if (dx * dx + dy * dy > 160.0f * 160.0f)
                continue;
            if (t->present)
                lorry_draw_trailer(&g->lorry, t->type, t->x, t->y, t->heading, &g->w);
            if (d->present)
                lorry_draw_trailer(&g->lorry, d->type, d->x, d->y, d->heading, &g->w);
        }
    }
    fx_draw_world(&g->fxs, &g->cam);
    if (g->rearcam)
        guides_draw(&r, &g->w);
    if (g->tourshots) {
        char name[16];
        snprintf(name, sizeof name, "P%d", (int)dgk_app.ticks - 1);
        if (dgk_app.ticks > 0 && (int)dgk_app.ticks <= g->tourshots)
            dgk_test_snapshot(name);                    /* the world and the lorry, no HUD */
        return;
    }
    hud_draw(g);
}

static void quit(void *u)
{
    game *g = (game *)u;
    career_save_now(g, "the end");
    sound_stop();
    if (g->record_path && !g->replay_path) {
        if (dgk_replay_save(g->replay, g->record_path) == 0)
            dgk_log("FW-RECORD %s ticks=%lu", g->record_path, (unsigned long)dgk_app.ticks);
        else
            dgk_log("FW-ERROR cannot write %s", g->record_path);
    }
    dgk_log("FW-RESULT ticks=%lu laps=%d delivered=%d money=%d damage=%.1f hits=%d jackknifes=%d desync=%d x=%.2f "
            "y=%.2f", (unsigned long)dgk_app.ticks, g->ap.laps, g->jobs.delivered, g->jobs.money, g->r.damage,
            g->r.hits, g->r.jackknifes, g->desync, g->r.x, g->r.y);
    if (g->fxtest)
        dgk_test_check("fx-caps", g->fxs.max_coins == FX_COINS && g->fxs.max_confetti == FX_CONFETTI &&
                       g->fxs.max_dust == FX_DUST, "at most %d coins, %d confetti, %d dust (caps %d, %d, %d)",
                       g->fxs.max_coins, g->fxs.max_confetti, g->fxs.max_dust, FX_COINS, FX_CONFETTI, FX_DUST);
    if (g->mode == MODE_JOB)
        dgk_test_check("job", g->jobs.delivered > 0 && g->jobs.grade >= GRADE_OK, "%s, %s in %.0f s, damage %.1f",
                       g->jobs.delivered ? "delivered" : "not delivered", grade_name[g->jobs.grade],
                       g->jobs.elapsed, g->r.damage);
    if (g->laps_wanted)
        dgk_test_check("laps", g->ap.laps >= g->laps_wanted, "%d of %d, damage %.1f, %d hits", g->ap.laps,
                       g->laps_wanted, g->r.damage, g->r.hits);
    if (g->replay_path)
        dgk_test_check("replay", !g->desync, "%s", g->desync ? "went elsewhere" : "the recorded state throughout");
    if (g->dbtest) {
        char notes[64];
        snprintf(notes, sizeof notes, "drive=%s ticks=%lu",
                 g->replay_path ? "replay" : g->mode != MODE_DRIVE ? "autopilot" : "keys",
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
    g.job_to = -1;
    g.start_money = -1;
    g.career_path = getenv("FW_CAREER");
    g.career_given = g.career_path != NULL;
    if (!g.career_path)
#ifdef DGK_DOS
        g.career_path = "CAREER.DAT";
#else
        g.career_path = "career.dat";
#endif
    g.cfg_path = getenv("FW_CFG");
    if (!g.cfg_path)
#ifdef DGK_DOS
        g.cfg_path = "FWHEEL.CFG";
#else
        g.cfg_path = "fwheel.cfg";
#endif
#ifdef DGK_DOS
    g.world_path = "WORLD.PAK";
#else
    g.world_path = "build/data/WORLD.PAK";
#endif
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-autopilot"))
            g.mode = MODE_TOUR;
        else if (!strcmp(a, "-autojob"))
            g.mode = MODE_JOB;
        else if (!strcmp(a, "-job") && v)
            sscanf(argv[++i], "%d:%d:%d", &g.job_from, &g.job_to, &g.job_bay);
        else if (!strcmp(a, "-hash"))
            g.hash = 1;
        else if (!strcmp(a, "-dockpose"))
            g.dockpose = 1;
        else if (!strcmp(a, "-soundtest"))
            g.soundtest = 1;
        else if (!strcmp(a, "-fxtest"))
            g.fxtest = 1;
        else if (!strcmp(a, "-calibrate"))
            g.calibrate_only = 1;
        else if (!strcmp(a, "-joylog"))
            g.joylog = 1;
        else if (!strcmp(a, "-career") && v) {
            g.career_path = argv[++i];
            g.career_given = 1;
        } else if (!strcmp(a, "-money") && v)
            g.start_money = atoi(argv[++i]);
        else if (!strcmp(a, "-trace") && v)
            g.trace = atoi(argv[++i]);
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
