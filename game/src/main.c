/* Fifth Wheel: deliveries with an articulated lorry.
 *
 *   FWHEEL [kit options, see dgk/app.h] [-world FILE] [-record FILE | -replay FILE] [-hash]
 *          [-trace N]                    the rig's pose every N ticks (FW-TRACE)
 *   FWHEEL -dockpose                     a trailer reversing into a bay, held still (pictures)
 *   FWHEEL -pose X:Y:H:V                 the rig held at (X, Y) heading H degrees, the camera
 *                                        as at V m/s (pictures of a place at speed)
 *   FWHEEL -soundtest                    each sound in turn, 2 s of silence, then quit (the SOUND suite)
 *   FWHEEL -musictest                    the driving tunes on the FM chip in a test (FW-MUSIC lines)
 *   FWHEEL -musictune NAME               that tune alone, looped (title, garage, drive1 ...)
 *   FWHEEL -fxtest                       every flourish far past its cap for 2 s (HX-TEST fx-caps)
 *          [-detail low|medium|high]     the detail preset (else FWHEEL.CFG's, else medium)
 *          [-zoom 0|1|2]                 the camera's zoom to start with (as the detail allows)
 *          [-title]                      the title screen, in a test too (else only when playing)
 *   FWHEEL -calibrate                    the joystick's calibration screen, saved, then quit
 *          [-joylog]                     the joystick's axes and mapped controls every 0.5 s (FW-JOY)
 *          [-career FILE] [-money N]     keep a career in FILE in a test run; start it with $N
 * FW_CRASH=N faults (#UD) at tick N: the crash test.
 * Settings: FWHEEL.CFG beside the game (fwheel.cfg on Linux; FW_CFG overrides). The career:
 * CAREER.DAT (career.dat; FW_CAREER), kept when playing (not in tests, replays or recordings).
 *   FWHEEL -autopilot [-laps N]          round the world's tour with a trailer
 *   FWHEEL -autojob [-job D:T:B]         the autopilot does the shortest job on offer at
 *                                        depot 0, or the job from depot D to depot T's bay B
 *                                        (from 0; FW-JOB lines; the test: graded OK or better)
 *   FWHEEL -autocareer N [-spender K]    a new career played by the autopilot until N depot
 *                                        jobs are delivered: jobs where it holds the licence
 *                                        (driving to another depot for one), licences bought
 *                                        when it can pay, repairs; else shunts, else parcel
 *                                        rounds (4 drops). -spender: first K cosmetics bought
 *                                        whenever it can (the test: never stuck)
 *   FWHEEL -autocareer 0 -rounds N       N parcel rounds by the autopilot, each from the next
 *                                        depot in turn (the town nearest it): the streets' ways
 *                                        in every town (the test: no knocks)
 *   FWHEEL -timedemo FILE -dbtest NAME   a replay as fast as it will go, timed
 *                                        as DOSBench test NAME (RESULTS.TXT)
 *   FWHEEL -probe [quick]                the performance probe (probe.h)
 *   FWHEEL -tourshots N                  N frames with the rig placed along the tour and at
 *                                        two depots, each saved as P<n> (DOS against Mesa)
 *
 * Playing starts at the title screen; Esc pauses (and offers the options).
 * Keys: arrows or WASD (steer, accelerate, brake; hold the brake at a stop
 * to reverse), Space the handbrake, H the horn, 1-3 take a job from the
 * board, 4 its shunt, R a parcel round (begin or end), Backspace cancel a job (before coupling), G the garage
 * (stopped), Z the zoom, J the joystick's set-up, Esc to quit. A joystick or wheel: steering, accelerator and brake as
 * calibrated, button 1 the handbrake, button 2 the horn. */
#include "game.h"
#include "music.h"
#include "ui.h"
#include "tex.h"
#include "dgk/bench.h"
#include "dgk/path.h"
#include "guides.h"
#include "dgk/save.h"
#include "probe.h"
#include "sky.h"
#include "sound.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const dgk_font_data fw_font;

enum { SC_A = 4, SC_D = 7, SC_G = 10, SC_H = 11, SC_J = 13, SC_R = 21, SC_Z = 29, SC_S = 22, SC_W = 26, SC_1 = 30,
       SC_BACKSPACE = 42 };

/* One tick of input, as recorded. Buttons: bit 0 the handbrake, bits 1-2
 * a job taken from the board (1-3), bit 3 the job cancelled, bit 4 the
 * horn held, bit 5 the board's shunt taken (4), bit 6 a parcel round begun
 * or ended (R). */
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
    if (dgk_app.key_pressed[SC_1 + JOB_SHUNT_KEY])
        f->buttons |= 32;
    if (dgk_app.key_pressed[SC_R])
        f->buttons |= 64;
}

/* The career: loaded at the start of play, saved after each delivery, each
 * purchase and at the end. Tests leave it alone unless given -career. */
static void career_save_now(game *g, const char *why)
{
    if (!g->careerful)
        return;
    g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
    g->car.damage10 = (uint32_t)(DGK_CLAMP(g->r.damage, 0.0f, 100.0f) * 10.0f + 0.5f);
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

/* The detail in force: the chosen preset, less the governor's notches. The
 * lorry is built again when its roundness changes. */
static void detail_apply(game *g)
{
    g->det = detail_effective(g->presets, g->detail_chosen, g->gov.notch);
    g->det.far = detail_far(&g->det, g->engine);
    g->fog_on = detail_fog(&g->det, g->engine);
    g->cam.pitch_min = g->det.pitch_min * 3.14159265f / 180.0f;
    g->w.models = g->det.models;                     /* the world's runs for this detail (CVAR) */
    if (g->det.models == 2 && world_load_hi(&g->w) && !g->w.atlas)
        g->w.atlas = tex_atlas();                    /* HIGH's textures: their world loaded the first time */
    g->w.textured = g->det.models == 2 && g->w.hi_state > 0 && g->w.atlas;
    if (g->lorry.quality != g->det.models) {
        lorry_build(&g->lorry, g->det.models);
        career_style(g);
    }
    traffic_build(g->det.models);
    fx_caps(&g->fxs, g->det.coins, g->det.confetti, g->det.dust);
    g->cam.zoom = DGK_MIN(g->cam.zoom, g->det.zoom_max);
}

/* The detail: -detail, else FWHEEL.CFG's, else the card's own (detail.h),
 * except in a test, where it is MEDIUM on every card so that the same frame
 * is the same picture on each. */
static void detail_start(game *g)
{
    int d = detail_parse(dgk_cfg_get(&g->cfg, "detail"));
    const char *renderer = (const char *)glGetString(GL_RENDERER), *why = "chosen";
    detail_load(g->presets, g->budget_path);
    /* (FW_ENGINE=1: the engine's paths taken anyway, to see them on another GL; 0: never) */
    g->engine = getenv("FW_ENGINE") ? atoi(getenv("FW_ENGINE")) != 0
                                    : detail_engine((const char *)glGetString(GL_EXTENSIONS));
    mb_engine(g->engine);
    g->w.engine = g->engine;
    if (g->detail_chosen < 0 && d >= 0) {
        g->detail_chosen = d;
        why = g->cfg_path;
    } else if (g->detail_chosen < 0 && dgk_test_active()) {
        g->detail_chosen = DETAIL_MEDIUM;
        why = "a test";
    } else if (g->detail_chosen < 0) {
        g->detail_chosen = detail_for_renderer(renderer);
        why = renderer ? renderer : "no renderer";
    }
    g->gov.on = !dgk_app.fixed && !dgk_test_active() && dgk_cfg_int(&g->cfg, "governor", 1);
    g->cam.zoom = g->zoom_wanted >= 0 ? g->zoom_wanted : 1;
    detail_apply(g);
    dgk_log("FW-CFG detail %s (%s), governor %s, transform engine %s, fog %s, far %.0f m", detail_name[g->detail_chosen],
            why, g->gov.on ? "on" : "off", g->engine ? "yes" : "no", g->fog_on ? "GL" : "haze", g->det.far);
}

/* -highlights' pieces and the gaps between them (below). */
enum { HL_COUPLE, HL_TO_TOWN, HL_TOWN, HL_TO_DOCK, HL_DOCK, HL_DONE };
static void hl_start(game *g, const char *piece);

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
        if (g->careerful)
            g->r.damage = DGK_MIN(g->car.damage10, 1000u) / 10.0f;   /* the damage the career left, until repaired */
        if (g->mode == MODE_CAREER) {                /* a new career: $250, box trailers only */
            g->jobs.money = (int)g->car.money;
            g->jobs.licences = career_licences(&g->car);
            jobpilot_reset(&g->jp, &g->jobs);
            g->jp.phase = JP_DONE;
        }
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
    } else if (g->mode == MODE_JOB || g->mode == MODE_CAREER) {
        dgk_log("FW-ERROR -autojob needs a world with depots");
        return -1;
    }
    if (g->posed || g->parked) {                     /* -pose: a box trailer behind, held at the place */
        rig_init(&g->r, g->pose[0], g->pose[1], g->pose[2] * WG_PI / 180.0f);
        g->r.v = g->posed ? g->pose[3] : 0;
    }
    g->r_prev = g->r;
    g->cam.zoom = 1;
    camera_reset(&g->cam, &g->r);
    g->cam_prev = g->cam;
    autopilot_reset(&g->ap, &g->r, &g->w);
    traffic_init(&g->traffic, &g->w, g->r.x, g->r.y, g->w.chunked ? g->ncars : 0);
    lorry_build(&g->lorry, 0);                       /* at the detail's roundness once it is known */
    ui_init();                                       /* the HUD's texture */
    if (dgk_font_load(&g->font, &fw_font) != 0)
        return -1;
    if (g->replay_path) {
        g->replay = dgk_replay_play(g->replay_path, (int)sizeof(input_frame));
        if (!g->replay)
            return -1;
    } else if (g->record_path)
        g->replay = dgk_replay_record((int)sizeof(input_frame), 0);
    sound_init();
    /* The music: when playing (not in a test unless -musictest: the sound suite listens for its own tones). */
    g->music_on = (!dgk_test_active() && !g->replay_path && !g->record_path && !g->soundtest) || g->musictest;
    if (g->music_on) {
        music_init();
        music_volume(DGK_CLAMP(dgk_cfg_int(&g->cfg, "sound.music", 7), 0, 10));
        if (g->music_tune && !music_play(g->music_tune))
            dgk_log("FW-MUSIC no tune %s", g->music_tune);
    }
    career_style(g);
    fx_init(&g->fxs, g->jobs.money);
    detail_start(g);
    menu_start(g, g->title_wanted || (g->mode == MODE_DRIVE && !dgk_test_active() && !g->replay_path &&
                                      !g->record_path && !g->tourshots && !g->dockpose && !g->soundtest &&
                                      !g->calibrate_only && !g->fxtest));
    if (g->dbtest) {
        dgk_bench_run("FWHEEL", "F2");
        if (!g->highlights)
            dgk_app_bench_start(g->dbtest, 30);
    }
    if (g->highlights) {
        if (g->mode != MODE_JOB) {
            dgk_log("FW-ERROR -highlights needs -autojob");
            return -1;
        }
        g->hl_phase = HL_COUPLE;
        hl_start(g, "couple");
    }
    glClearColor(((SKY_HORIZON >> 16) & 255) / 255.0f, ((SKY_HORIZON >> 8) & 255) / 255.0f, (SKY_HORIZON & 255) / 255.0f,
                 1);
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

/* ---- -autocareer: a new career played by the autopilot (the career's tests) ---- */

/* At a stop: the spender's cosmetics (the cheapest it can pay for), then the licences in turn when it can pay,
 * and repairs past 40 points. */
static void career_shop(game *g)
{
    career *c = &g->car;
    int i, best;
    c->money = (uint32_t)DGK_MAX(g->jobs.money, 0);
    c->damage10 = (uint32_t)(DGK_CLAMP(g->r.damage, 0.0f, 100.0f) * 10.0f + 0.5f);
    for (;;) {
        best = -1;
        for (i = 0; i < nitems && g->career_spend > 0; i++)
            if (items[i].kind != KIND_LICENCE && !career_owns(c, i) && (uint32_t)items[i].price <= c->money &&
                (best < 0 || items[i].price < items[best].price))
                best = i;
        for (i = 0; i < nitems && best < 0; i++)
            if (items[i].kind == KIND_LICENCE && !career_owns(c, i)) {
                if ((uint32_t)items[i].price <= c->money)
                    best = i;
                break;
            }
        if (best < 0 || career_buy(c, best) != 0)
            break;
        if (items[best].kind != KIND_LICENCE)
            g->career_spend--;
        dgk_log("FW-CAREERPILOT buy %s for $%d: money %lu", items[best].name, items[best].price,
                (unsigned long)c->money);
    }
    if (c->damage10 > 400) {
        int paid = career_repair(c);
        if (paid)
            dgk_log("FW-CAREERPILOT repair for $%d: damage %.1f", paid, c->damage10 / 10.0f);
    }
    g->jobs.money = (int)c->money;
    g->jobs.licences = career_licences(c);
    g->r.damage = c->damage10 / 10.0f;
}

/* Stopped with nothing on: a job here if it may take this depot's trailer (the shortest); else to the
 * nearest depot with a trailer it may take; else a shunt here (two in a row at most); else a parcel round. */
static void career_decide(game *g, input_frame *f)
{
    jobs *j = &g->jobs;
    int i, best = -1, here = j->at_depot;
    float bd = 1e30f;
    if (here >= 0 && j->offers_at != here)
        return;                                      /* (the board comes next tick) */
    if (g->cp_rounds) {
        /* -rounds: a round, then to the next depot in turn, then a round in the town nearest it, ... */
        if (j->rounds >= g->cp_rounds)
            return;
        if (g->cp_rounds_seen) {                     /* (the last thing done was a round) */
            int d = ++g->cp_trips % g->w.ndepots;
            g->cp_rounds_seen = 0;
            jobs_travel(j, &g->w, &g->r, d);
            jobpilot_travel(&g->jp, j, &g->r, &g->w, d);
            dgk_log("FW-CAREERPILOT to depot %d for the next round", d);
            return;
        }
        f->buttons |= 64;
        g->cp_rounds_seen = 1;
        j->round_town = j->rounds % g->w.ntowns;      /* every town in turn */
        dgk_log("FW-CAREERPILOT parcel round from depot %d in town %d", here, j->round_town);
        return;
    }
    career_shop(g);
    if (here >= 0 && j->trailers[here].present && (j->licences >> j->trailers[here].type & 1u)) {
        int k, b = 0;
        for (k = 1; k < JOB_OFFERS; k++)
            if (j->offers[k].distance < j->offers[b].distance)
                b = k;
        f->buttons |= (uint8_t)((b + 1) << 1);
        g->cp_shunts = 0;
        dgk_log("FW-CAREERPILOT job at depot %d: the %s to depot %d, money %d", here,
                trailer_name[j->trailers[here].type], j->offers[b].to, j->money);
        return;
    }
    for (i = 0; i < g->w.ndepots; i++) {
        float dx = g->w.depots[i].x - g->r.x, dy = g->w.depots[i].y - g->r.y;
        if (i != here && j->trailers[i].present && (j->licences >> j->trailers[i].type & 1u) &&
            dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    if (best >= 0) {
        jobs_travel(j, &g->w, &g->r, best);
        jobpilot_travel(&g->jp, j, &g->r, &g->w, best);
        g->cp_shunts = 0;
        dgk_log("FW-CAREERPILOT to depot %d for its %s, money %d", best, trailer_name[j->trailers[best].type],
                j->money);
        return;
    }
    if (here >= 0 && j->shunt.kind == JOB_KIND_SHUNT && g->cp_shunts < 2) {
        f->buttons |= 32;
        g->cp_shunts++;
        dgk_log("FW-CAREERPILOT shunt at depot %d, money %d", here, j->money);
        return;
    }
    f->buttons |= 64;
    g->cp_shunts = 0;
    dgk_log("FW-CAREERPILOT parcel round, money %d", j->money);
}

static void career_pilot(game *g, input_frame *f)
{
    jobs *j = &g->jobs;
    rig_input a;
    int mark = j->delivered + j->shunts + j->drops;
    if (mark != g->cp_mark || !g->cp_tick) {
        g->cp_mark = mark;
        g->cp_tick = dgk_app.ticks;
    } else if (dgk_app.ticks - g->cp_tick > 150000ul && !g->cp_stuck) {  /* 2,500 s with nothing done */
        g->cp_stuck = 1;
        dgk_log("FW-CAREERPILOT stuck: state %d kind %d, pilot phase %d, money %d, licences %lu", j->state,
                j->current.kind, g->jp.phase, j->money, (unsigned long)j->licences);
        dgk_app.quit = 1;
    }
    f->buttons = 0;
    if (j->state == JOB_HAULING && j->current.kind == JOB_KIND_PARCEL && j->round_drops >= 4)
        f->buttons |= 64;                            /* four drops: the round over */
    else if (j->state == JOB_NONE && !j->pending && jobpilot_idle(&g->jp, &g->r) &&
             (j->delivered < g->career_want || j->rounds < g->cp_rounds) &&
             dgk_app.ticks > 1)                      /* (after the first tick has found the depot it is on) */
        career_decide(g, f);
    jobpilot_drive(&g->jp, j, &g->r, &g->w, &g->traffic, &a);
    f->steer = (int8_t)(a.steer * 127.0f);
    f->accel = (uint8_t)(a.accel * 255.0f);
    f->decel = (uint8_t)(a.decel * 255.0f);
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
        fx_coins(&g->fxs, DGK_CLAMP(j->earned / 40, 4, g->det.coins));
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
    } else if (j->event == JOB_EVENT_DROPPED) {
        sound_chime(j->streak >= 3 ? GRADE_GREAT : GRADE_GOOD);
        fx_coins(&g->fxs, DGK_CLAMP(j->earned / 8, 3, g->det.coins));
        career_save_now(g, "parcel");
        snprintf(g->callout_buf, sizeof g->callout_buf, "+$%d", j->earned);
        hud_callout(g, j->streak >= 2 ? "ON A ROLL!" : "PARCEL DROPPED", g->callout_buf,
                    j->streak >= 2 ? 0x5FD8FFFFu : 0x9FFFB0FFu);
    } else if (j->event == JOB_EVENT_SHUNTED) {
        static const char *const shunted[] = { "", "SHUNTED", "GOOD PARK!", "GREAT PARK!", "PERFECT PARK!" };
        sound_chime(j->grade);
        fx_coins(&g->fxs, DGK_CLAMP(j->earned / 10, 3, g->det.coins));
        career_save_now(g, "shunt");
        snprintf(g->callout_buf, sizeof g->callout_buf, "+$%d", j->earned);
        hud_callout(g, shunted[j->grade], g->callout_buf, 0x9FFFB0FFu);
    } else if (j->event == JOB_EVENT_ROUND_OVER) {
        snprintf(g->callout_buf, sizeof g->callout_buf, "%d DROPS  $%d", j->round_drops, j->round_earned);
        hud_callout(g, "ROUND OVER", g->callout_buf, 0xFFFFFFFFu);
        career_save_now(g, "round");
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

static void drive_tick(game *g);

/* -highlights (FW3, docs/perf.md): three pieces of a job timed, each its own DOSBench test (-dbtest NAME:
 * NAME-couple, NAME-town, NAME-dock), the drive between them fast-forwarded (eight steps a frame, nothing drawn
 * but a cleared screen) and not timed. The pieces come from the job, not from tick numbers: from the start to 4 s
 * after the coupling; while within 110 m of a town's middle (the first the rig comes to, hauling); reversing, from
 * the trailer's axle 22 m from its place in the bay to the delivery, and then it quits. */
#define HL_TOWN_IN 110.0f
#define HL_TOWN_OUT 120.0f
#define HL_BAY 22.0f

static void hl_start(game *g, const char *piece)
{
    char name[32];
    snprintf(name, sizeof name, "%s-%s", g->dbtest ? g->dbtest : "FW3", piece);
    dgk_log("FW-BENCH %s from step %lu", name, (unsigned long)g->steps);
    if (g->dbtest)
        dgk_app_bench_start(name, 30);
    g->hl_ff = 0;
    g->hl_mark = g->steps;
}

static void hl_stop(game *g, int next)
{
    char notes[64];
    snprintf(notes, sizeof notes, "drive=autopilot steps=%lu", (unsigned long)(g->steps - g->hl_mark));
    dgk_log("FW-BENCH piece over at step %lu (%s)", (unsigned long)g->steps, notes);
    if (g->dbtest)
        dgk_app_bench_stop("ok", notes);
    g->hl_phase = next;
    g->hl_ff = next != HL_DONE;
}

/* The town within `r` of (x, y) (its node), or -1. */
static int town_near(const game *g, float x, float y, float r)
{
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    const wg_graph *gr = world_graph(&g->w, &n, &e, &p);
    int i;
    for (i = 0; gr && i < (int)gr->nnodes; i++)
        if (n[i].kind == WG_TOWN && (n[i].x - x) * (n[i].x - x) + (n[i].y - y) * (n[i].y - y) < r * r)
            return i;
    return -1;
}

static void highlights(game *g)
{
    switch (g->hl_phase) {
    case HL_COUPLE:
        if (g->jobs.state >= JOB_HAULING && !g->hl_coupled)
            g->hl_coupled = g->steps;
        if (g->hl_coupled && g->steps - g->hl_coupled >= 240)
            hl_stop(g, HL_TO_TOWN);
        break;
    case HL_TO_TOWN:
        if (g->jobs.state == JOB_HAULING && (g->hl_town = town_near(g, g->r.x, g->r.y, HL_TOWN_IN)) >= 0) {
            g->hl_phase = HL_TOWN;
            hl_start(g, "town");
        }
        break;
    case HL_TOWN: {
        const wg_node *n;
        const wg_edge *e;
        const float *p;
        world_graph(&g->w, &n, &e, &p);
        if ((n[g->hl_town].x - g->r.x) * (n[g->hl_town].x - g->r.x) +
                (n[g->hl_town].y - g->r.y) * (n[g->hl_town].y - g->r.y) > HL_TOWN_OUT * HL_TOWN_OUT)
            hl_stop(g, HL_TO_DOCK);
        break;
    }
    case HL_TO_DOCK:
        if (g->jp.phase == JP_REVERSE) {
            float ax, ay, bx, by, bh;
            rig_trailer_axle(&g->r, &ax, &ay);
            jobs_bay(&g->jobs, &g->w, &bx, &by, &bh);
            if ((ax - bx) * (ax - bx) + (ay - by) * (ay - by) < HL_BAY * HL_BAY) {
                g->hl_phase = HL_DOCK;
                hl_start(g, "dock");
            }
        }
        break;
    case HL_DOCK:
        if (g->jp.phase == JP_DONE || g->jobs.delivered > g->jp.delivered) {
            hl_stop(g, HL_DONE);
            dgk_app.quit = 1;
        }
        break;
    default:
        break;
    }
}

/* The tune for what is on the screen (the title's, the garage's, else the driving tunes), and what is due of
 * it: at every tick and every frame. */
static void music_now(game *g)
{
    if (!g->music_on)
        return;
    if (!g->music_tune)
        music_want(g->musictest ? MUSIC_DRIVING : g->gar.open ? MUSIC_GARAGE
                   : g->men.screen == SCREEN_TITLE || (g->men.screen == SCREEN_OPTIONS && g->men.back == SCREEN_TITLE)
                       ? MUSIC_TITLE : MUSIC_DRIVING);
    music_update();
}

static void tick(void *u)
{
    game *g = (game *)u;
    int k;
    music_now(g);
    if (g->tourshots) {                              /* pose k is drawn and saved in frame k + 1 */
        if ((int)dgk_app.ticks >= g->tourshots) {
            dgk_app.quit = 1;
            return;
        }
        pose(g, (int)dgk_app.ticks);
        return;
    }
    if (g->crash_at && dgk_app.ticks == g->crash_at) {   /* FW_CRASH=N: a fault at tick N (the crash test) */
        dgk_log("FW-CRASH at tick %lu", (unsigned long)dgk_app.ticks);
#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("ud2");
#else
        abort();
#endif
    }
    if (g->cal.step != CAL_OFF) {
        calibration(g);
        return;
    }
    if (g->posed) {                                  /* held; only the camera moves, as if at speed */
        g->r_prev = g->r;
        g->cam_prev = g->cam;
        camera_tick(&g->cam, &g->r, 1.0f / DGK_TICK_HZ, NULL);
        return;
    }
    if (g->gar.open) {                               /* the garage: the world waits */
        int changed;
        g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
        g->car.damage10 = (uint32_t)(DGK_CLAMP(g->r.damage, 0.0f, 100.0f) * 10.0f + 0.5f);
        changed = garage_tick(&g->gar, &g->car, &g->lorry);
        g->jobs.money = (int)g->car.money;           /* what was spent, before the save copies it back */
        g->r.damage = g->car.damage10 / 10.0f;       /* and what was repaired */
        if (g->careerful)
            g->jobs.licences = career_licences(&g->car);
        if (changed)
            career_save_now(g, "garage");
        if (!g->gar.open)
            career_style(g);                         /* what is fitted, not what was tried on */
        return;
    }
    if (menu_tick(g))                                /* the title, pause or options: the world waits */
        return;
    if (dgk_app.key_pressed[SC_Z]) {                 /* zoom: near, normal, far (as the detail allows) */
        g->cam.zoom = g->cam.zoom >= g->det.zoom_max ? 0 : g->cam.zoom + 1;
        dgk_log("FW-VIEW zoom %d", g->cam.zoom);
    }
    if (dgk_app.key_pressed[SC_G] && fabsf(g->r.v) < 0.5f && g->has_jobs && !g->replay_path && !g->record_path &&
        g->mode == MODE_DRIVE) {
        g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
        g->car.damage10 = (uint32_t)(DGK_CLAMP(g->r.damage, 0.0f, 100.0f) * 10.0f + 0.5f);
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
    if (dgk_app.key_pressed[DGK_KEY_ESCAPE]) {      /* a pause, unless recording or replaying */
        if (g->mode == MODE_DRIVE && !g->replay_path && !g->record_path) {
            menu_open(g, SCREEN_PAUSE);
            return;
        }
        dgk_app.quit = 1;
    }
    /* The drive: a step a tick; fast-forwarding between the highlights' pieces (-highlights), eight. */
    for (k = 0; k < (g->hl_ff ? 8 : 1); k++) {
        if (k && dgk_app.quit)
            break;
        drive_tick(g);
        if (g->highlights)
            highlights(g);
    }
}

/* One step of the drive: the inputs (the autopilots', a replay's, the player's), the rig, the cars, the job, the
 * camera, the sounds and flourishes, the hashes and logs. */
static void drive_tick(game *g)
{
    input_frame f;
    rig_input in;
    uint32_t h;
    g->steps++;
    if (g->dockpose) {                               /* held still, the wheels turned */
        f.steer = 60;
        f.accel = f.decel = f.buttons = 0;
    } else if (g->replay_path) {
        if (!dgk_replay_frame(g->replay, &f)) {
            dgk_log("FW-REPLAY end at tick %lu", (unsigned long)dgk_app.ticks);
            dgk_app.quit = 1;
            return;
        }
    } else if (g->mode == MODE_CAREER)
        career_pilot(g, &f);
    else if (g->mode == MODE_TOUR || g->mode == MODE_JOB) {
        rig_input a;
        if (g->mode == MODE_TOUR)
            autopilot_drive(&g->ap, &g->r, &g->w, &g->traffic, &a);
        else
            jobpilot_drive(&g->jp, &g->jobs, &g->r, &g->w, &g->traffic, &a);
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
        if (f.buttons & 32)
            jobs_take_shunt(&g->jobs, &g->r);
        if (f.buttons & 64)
            jobs_round(&g->jobs, &g->w, &g->r);
        jobs_props(&g->jobs, &g->w, g->r.x, g->r.y);
    }
    rig_step(&g->r, &in, 1.0f / DGK_TICK_HZ, &g->w);
    traffic_tick(&g->traffic, &g->w, &g->r, 1.0f / DGK_TICK_HZ);
    if (g->has_jobs)
        job_events(g);
    {
        camera_focus f;
        int focus = g->has_jobs && camera_focus_for(g, &f);
        camera_tick(&g->cam, &g->r, 1.0f / DGK_TICK_HZ, focus ? &f : NULL);
        g->rearcam = focus && g->r.gear < 0;          /* the reversing camera's look and guides */
    }
    if (!g->musictest)                               /* (-musictest: the music alone) */
        sound_tick(&g->r, f.buttons & 16);
    fx_tick(&g->fxs, &g->r, &g->w, g->jobs.money);
    if (g->fxs.coins_landed)
        sound_coin();
    if (g->r.hits != g->hits_seen) {                 /* a knock: the cab jolts, dust flies */
        g->hits_seen = g->r.hits;
        dgk_log("FW-EVENT hit tick=%lu x=%.1f y=%.1f speed=%.1f damage=%.1f", (unsigned long)dgk_app.ticks, g->r.x,
                g->r.y, g->r.v, g->r.damage);
        fx_bump(&g->fxs, 0.9f);
        fx_dust_at(&g->fxs, &g->w, g->r.x + 4.5f * cosf(g->r.heading), g->r.y + 4.5f * sinf(g->r.heading), 3, 2.0f);
    }
    if (g->mode == MODE_TOUR && g->replay_path) {
        rig_input unused;
        autopilot_drive(&g->ap, &g->r, &g->w, NULL, &unused);  /* keeps counting laps */
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
    h = traffic_hash(&g->traffic, rig_hash(&g->r, 2166136261u));
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
        traffic_log(&g->traffic);
    }
    /* In a test, a line every 10 s of the game's time: a long run in 86Box (a haul drawn at a few frames a
     * second) is otherwise silent long enough for the harness to call it a hang. */
    if (dgk_test_active() && (dgk_app.ticks + 1) % 600 == 0)
        dgk_log("FW-TICK %lu", (unsigned long)dgk_app.ticks + 1);
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
    if (g->mode == MODE_CAREER && g->jobs.delivered >= g->career_want && g->jobs.rounds >= g->cp_rounds &&
        g->jobs.state == JOB_NONE && !g->done_tick)
        g->done_tick = dgk_app.ticks;
    if (g->mode == MODE_CAREER && g->done_tick && dgk_app.ticks - g->done_tick >= 150)
        dgk_app.quit = 1;
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

/* The trailers standing at depots within the detail's reach of the rig:
 * one waiting and one delivered at most at each. */
static int parked_near(const game *g, const rig *r, const parked **out)
{
    int i, n = 0;
    if (!g->has_jobs)
        return 0;
    for (i = 0; i < g->w.ndepots; i++) {
        float dx = g->w.depots[i].x - r->x, dy = g->w.depots[i].y - r->y;
        if (dx * dx + dy * dy > g->det.trailers * g->det.trailers)
            continue;
        if (g->jobs.trailers[i].present)
            out[n++] = &g->jobs.trailers[i];
        if (g->jobs.docked[i].present)
            out[n++] = &g->jobs.docked[i];
    }
    return n;
}

static void draw(void *u, float alpha)
{
    game *g = (game *)u;
    float view_pitch = 1.0f, view_eye = 30.0f;     /* the camera, for the haze after the world */
    music_now(g);
    if (dgk_app.frame_us && governor_frame(&g->gov, dgk_app.frame_us / 1000.0f))
        detail_apply(g);                             /* looks only: never the simulation */
    if (g->gar.open) {
        garage_draw(&g->gar, &g->car, &g->lorry, &g->font, alpha);
        return;
    }
    if (g->cal.step == CAL_OFF && menu_draw_scene(g, alpha))
        return;
    if (g->hl_ff) {                                  /* between the highlights' pieces: nothing to time */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        dgk_gfx_overlay_begin();
        dgk_text(&g->font, 230, 230, 1.0f, 0xFFFFFFFFu, "FAST FORWARD");
        dgk_gfx_overlay_end();
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
        float pitch, eye_h;
        camera_apply(&g->cam, &g->cam_prev, alpha, (float)dgk_app.width / dgk_app.height, world_height(&g->w, tx, ty),
                     g->det.far, &pitch, &eye_h);
        view_pitch = pitch;
        view_eye = eye_h;
        sky_draw(pitch);
        if (g->fog_on)
            sky_fog(pitch, eye_h, g->det.far);
        /* As far round the target as the camera sees ahead of the eye (the eye is behind it), and no less than
         * 0.65 of the far plane (the sides of a steep view); the view's planes cull the rest. */
        world_draw(&g->w, tx, ty, DGK_MAX(g->det.far * 0.65f, sky_seen(pitch, eye_h, g->det.far)));
    }
    {
        const parked *standing[WG_MAX_PLACES * 2];
        int i, n = parked_near(g, &r, standing);
        if (g->has_jobs) {
            route_draw(&g->jobs, &r, &g->w, dgk_app.ticks);
            bay_draw(&g->jobs, &r, &g->w, dgk_app.ticks);
        }
        float reach = g->det.far * 0.65f;
        lorry_shadows_begin();
        lorry_shadow_rig(&r, &g->w);
        for (i = 0; i < n; i++)
            lorry_shadow_trailer(standing[i]->x, standing[i]->y, standing[i]->heading, &g->w);
        traffic_shadows(&g->traffic, &g->w, alpha, r.x, r.y, reach);
        lorry_shadows_end();
        mb_light_begin();                            /* (GL lights them on the transform engine: mesh.h) */
        lorry_draw(&g->lorry, &r, &g->w, &g->fxs.sway);
        traffic_draw(&g->traffic, &g->w, alpha, r.x, r.y, reach);
        for (i = 0; i < n; i++)
            lorry_draw_trailer(&g->lorry, standing[i]->type, standing[i]->x, standing[i]->y, standing[i]->heading,
                               &g->w);
        mb_light_end();
    }
    if (g->has_jobs)
        target_arrow_draw(&g->jobs, &r, &g->w, dgk_app.ticks);
    fx_draw_world(&g->fxs, &g->cam);
    glDisable(GL_FOG);
    if (!g->fog_on)
        sky_haze(view_pitch, view_eye, g->det.far);
    if (g->rearcam)
        guides_draw(&r, &g->w);
    if (g->tourshots) {
        char name[16];
        snprintf(name, sizeof name, "P%d", (int)dgk_app.ticks - 1);
        if (dgk_app.ticks > 0 && (int)dgk_app.ticks <= g->tourshots)
            dgk_test_snapshot(name);                    /* the world and the lorry, no HUD */
        return;
    }
    /* The sound test's frames are the world without the HUD. 86Box records the WAV in real time, and its
     * Matrox draws the HUD's textured quads (some 50,000 filtered, blended pixels a frame) so slowly that the
     * recording lost nearly all the sound (2026-11-20; on a card it costs about a millisecond). The picture
     * is nothing to that check. */
    if (!g->soundtest)
        hud_draw(g);
    menu_draw_overlay(g);
}

static void quit(void *u)
{
    game *g = (game *)u;
    career_save_now(g, "the end");
    if (g->music_on)
        music_shutdown();
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
        dgk_test_check("fx-caps", g->fxs.max_coins == g->det.coins && g->fxs.max_confetti == g->det.confetti &&
                       g->fxs.max_dust == g->det.dust, "at most %d coins, %d confetti, %d dust (%s: %d, %d, %d)",
                       g->fxs.max_coins, g->fxs.max_confetti, g->fxs.max_dust, detail_name[g->detail_chosen],
                       g->det.coins, g->det.confetti, g->det.dust);
    if (g->mode == MODE_JOB)
        dgk_test_check("job", g->jobs.delivered > 0 && g->jobs.grade >= GRADE_OK, "%s, %s in %.0f s, damage %.1f",
                       g->jobs.delivered ? "delivered" : "not delivered", grade_name[g->jobs.grade],
                       g->jobs.elapsed, g->r.damage);
    if (g->mode == MODE_CAREER)
        /* (-rounds: the streets' ways must not touch anything solid; a car's knock is the traffic's business) */
        dgk_test_check("career", g->jobs.delivered >= g->career_want && g->jobs.rounds >= g->cp_rounds &&
                       g->jobs.drops >= 4 * g->cp_rounds && !g->cp_stuck &&
                       (!g->cp_rounds || g->r.hits == g->traffic.knocks),
                       "%d of %d delivered, %d shunts, %d parcels in %d rounds, money %d, licences %lu, damage %.1f, "
                       "%d knocks (%d of them cars)%s", g->jobs.delivered, g->career_want, g->jobs.shunts,
                       g->jobs.drops, g->jobs.rounds, g->jobs.money, (unsigned long)g->jobs.licences, g->r.damage,
                       g->r.hits, g->traffic.knocks, g->cp_stuck ? ", STUCK" : "");
    if (g->laps_wanted)
        dgk_test_check("laps", g->ap.laps >= g->laps_wanted, "%d of %d, damage %.1f, %d hits", g->ap.laps,
                       g->laps_wanted, g->r.damage, g->r.hits);
    if (g->replay_path)
        dgk_test_check("replay", !g->desync, "%s", g->desync ? "went elsewhere" : "the recorded state throughout");
    if (g->dbtest && !g->highlights) {
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
    g.ncars = TRAFFIC_CARS;
    g.start_money = -1;
    g.detail_chosen = -1;
    g.zoom_wanted = -1;
    /* The files by their DOS names (dgk/path.h): off DOS the data are the build's, the career and settings
     * the current directory's, in lower case. */
    dgk_path_dir(DGK_DATA, "build/data");
    g.budget_path = dgk_path(DGK_DATA, "budget.cfg");
    g.career_path = getenv("FW_CAREER");
    g.career_given = g.career_path != NULL;
    if (!g.career_path)
        g.career_path = dgk_path(DGK_USER, "CAREER.DAT");
    if (getenv("FW_CRASH"))
        g.crash_at = (uint32_t)DGK_MAX(atoi(getenv("FW_CRASH")), 1);
    g.cfg_path = getenv("FW_CFG");
    if (!g.cfg_path)
        g.cfg_path = dgk_path(DGK_USER, "FWHEEL.CFG");
    g.world_path = dgk_path(DGK_DATA, "WORLD.PAK");
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-autopilot"))
            g.mode = MODE_TOUR;
        else if (!strcmp(a, "-cars") && v)
            g.ncars = atoi(argv[++i]);
        else if (!strcmp(a, "-autojob"))
            g.mode = MODE_JOB;
        else if (!strcmp(a, "-job") && v)
            sscanf(argv[++i], "%d:%d:%d", &g.job_from, &g.job_to, &g.job_bay);
        else if (!strcmp(a, "-hash"))
            g.hash = 1;
        else if (!strcmp(a, "-dockpose"))
            g.dockpose = 1;
        else if (!strcmp(a, "-pose") && v)
            g.posed = sscanf(argv[++i], "%f:%f:%f:%f", &g.pose[0], &g.pose[1], &g.pose[2], &g.pose[3]) == 4;
        else if (!strcmp(a, "-highlights"))
            g.highlights = 1;
        else if (!strcmp(a, "-autocareer") && v) {
            g.mode = MODE_CAREER;
            g.career_want = atoi(argv[++i]);
        } else if (!strcmp(a, "-spender") && v)
            g.career_spend = atoi(argv[++i]);
        else if (!strcmp(a, "-rounds") && v)
            g.cp_rounds = atoi(argv[++i]);
        else if (!strcmp(a, "-park") && v)
            g.parked = sscanf(argv[++i], "%f:%f:%f", &g.pose[0], &g.pose[1], &g.pose[2]) == 3;
        else if (!strcmp(a, "-soundtest"))
            g.soundtest = 1;
        else if (!strcmp(a, "-musictest"))
            g.musictest = 1;
        else if (!strcmp(a, "-musictune") && v) {
            g.musictest = 1;
            g.music_tune = argv[++i];
        }
        else if (!strcmp(a, "-fxtest"))
            g.fxtest = 1;
        else if (!strcmp(a, "-detail") && v)
            g.detail_chosen = detail_parse(argv[++i]);
        else if (!strcmp(a, "-zoom") && v)
            g.zoom_wanted = atoi(argv[++i]);
        else if (!strcmp(a, "-title"))
            g.title_wanted = 1;
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
    {
        /* The screen size and vsync from the settings, unless the command line says. */
        static char *args[64], mode[16];
        static dgk_cfg early;
        int n = 0, has_mode = 0, has_vsync = 0;
        for (i = 0; i < argc && n < 60; i++) {
            has_mode |= !strcmp(argv[i], "-mode");
            has_vsync |= !strcmp(argv[i], "-novsync");
            args[n++] = argv[i];
        }
        if (dgk_cfg_load(&early, g.cfg_path) == 0) {
            const char *m = dgk_cfg_get(&early, "video.mode");
            if (m && !has_mode) {
                snprintf(mode, sizeof mode, "%s", m);
                args[n++] = (char *)"-mode";
                args[n++] = mode;
            }
            if (!dgk_cfg_int(&early, "video.vsync", 1) && !has_vsync)
                args[n++] = (char *)"-novsync";
        }
        args[n] = NULL;
        return dgk_app_run(&desc, &g, n, args);
    }
}
