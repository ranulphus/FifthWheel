/* game.h - the game's state, shared by the loop (main.c) and the HUD
 * (hud.c). */
#ifndef FW_GAME_H
#define FW_GAME_H

#include "dgk/dgk.h"
#include "dgk/replay.h"
#include "autopilot.h"
#include "camera.h"
#include "input.h"
#include "jobpilot.h"
#include "jobs.h"
#include "lorry.h"
#include "vehicle.h"
#include "world.h"

enum { MODE_DRIVE, MODE_TOUR, MODE_JOB };   /* keys (jobs where there are depots); -autopilot; -autojob */

typedef struct game {
    const char *world_path, *record_path, *replay_path, *dbtest;
    int mode, laps_wanted, hash, trace, desync, last_laps, tourshots;
    int job_from, job_to, job_bay;  /* -job D:T:B (job_to -1: none) */
    int dockpose;                   /* -dockpose */
    int calibrate_only, joylog;     /* -calibrate (then quit), -joylog */
    dgk_cfg cfg;                    /* the settings file */
    const char *cfg_path;
    joymap jmap;                    /* the joystick's controls */
    calib cal;                      /* the calibration screen (cal.step CAL_OFF: closed) */
    int soundtest;                  /* -soundtest */
    int rearcam;                    /* coupling or docking in reverse: the reversing camera's look */
    world w;
    rig r, r_prev;
    camera cam, cam_prev;
    autopilot ap;
    jobs jobs;
    int has_jobs;               /* the world has depots, and this is not the tour */
    jobpilot jp;
    int autotake;               /* -autojob: the offer to take (1-3) in the first tick */
    uint32_t done_tick;         /* -autojob: when the delivery was made */
    lorry_meshes lorry;
    dgk_font font;
    dgk_replay *replay;
    float steer, accel, decel;  /* keyboard, smoothed */
    int jackknifes_seen;
    const char *callout, *callout2;
    char callout_buf[24];
    uint32_t callout_rgba, callout_tick;
    uint64_t fps_t0;
    uint32_t fps_frames;
    float fps;
} game;

void hud_draw(game *g);
void hud_callout(game *g, const char *text, const char *text2, uint32_t rgba);

#endif
