/* input.c - joystick mapping and calibration (see input.h). */
#include "input.h"
#include "dgk/log.h"
#include <stdlib.h>

#define MOVED 12000                 /* an axis this far from rest has been moved on purpose */
#define BACK 4000                   /* and this near it has come back */

void joymap_default(joymap *m)
{
    m->steer_axis = 0;
    m->steer_rest = 0;
    m->steer_left = -32767;
    m->steer_right = 32767;
    m->accel_axis = 1;
    m->accel_rest = 0;
    m->accel_full = -32767;
    m->brake_axis = 1;
    m->brake_rest = 0;
    m->brake_full = 32767;
    m->deadzone = 60;
    m->handbrake_button = 0;
    m->horn_button = 1;
    m->calibrated = 0;
}

void joymap_load(joymap *m, const dgk_cfg *c)
{
    joymap_default(m);
    if (!dgk_cfg_get(c, "joy.steer.axis"))
        return;
    m->steer_axis = DGK_CLAMP(dgk_cfg_int(c, "joy.steer.axis", 0), 0, DGK_JOY_AXES - 1);
    m->steer_rest = dgk_cfg_int(c, "joy.steer.rest", 0);
    m->steer_left = dgk_cfg_int(c, "joy.steer.left", -32767);
    m->steer_right = dgk_cfg_int(c, "joy.steer.right", 32767);
    m->accel_axis = DGK_CLAMP(dgk_cfg_int(c, "joy.accel.axis", 1), 0, DGK_JOY_AXES - 1);
    m->accel_rest = dgk_cfg_int(c, "joy.accel.rest", 0);
    m->accel_full = dgk_cfg_int(c, "joy.accel.full", -32767);
    m->brake_axis = DGK_CLAMP(dgk_cfg_int(c, "joy.brake.axis", 1), 0, DGK_JOY_AXES - 1);
    m->brake_rest = dgk_cfg_int(c, "joy.brake.rest", 0);
    m->brake_full = dgk_cfg_int(c, "joy.brake.full", 32767);
    m->deadzone = DGK_CLAMP(dgk_cfg_int(c, "joy.deadzone", 60), 0, 500);
    m->handbrake_button = DGK_CLAMP(dgk_cfg_int(c, "joy.handbrake", 0), 0, DGK_JOY_BUTTONS - 1);
    m->horn_button = DGK_CLAMP(dgk_cfg_int(c, "joy.horn", 1), 0, DGK_JOY_BUTTONS - 1);
    m->calibrated = 1;
}

void joymap_store(const joymap *m, dgk_cfg *c)
{
    dgk_cfg_set_int(c, "joy.steer.axis", m->steer_axis);
    dgk_cfg_set_int(c, "joy.steer.rest", m->steer_rest);
    dgk_cfg_set_int(c, "joy.steer.left", m->steer_left);
    dgk_cfg_set_int(c, "joy.steer.right", m->steer_right);
    dgk_cfg_set_int(c, "joy.accel.axis", m->accel_axis);
    dgk_cfg_set_int(c, "joy.accel.rest", m->accel_rest);
    dgk_cfg_set_int(c, "joy.accel.full", m->accel_full);
    dgk_cfg_set_int(c, "joy.brake.axis", m->brake_axis);
    dgk_cfg_set_int(c, "joy.brake.rest", m->brake_rest);
    dgk_cfg_set_int(c, "joy.brake.full", m->brake_full);
    dgk_cfg_set_int(c, "joy.deadzone", m->deadzone);
    dgk_cfg_set_int(c, "joy.handbrake", m->handbrake_button);
    dgk_cfg_set_int(c, "joy.horn", m->horn_button);
}

/* How far v has gone from rest towards end: 0..1, less the dead zone. */
static float travel(int v, int rest, int end, int deadzone)
{
    float dead = deadzone / 1000.0f, t;
    if (end == rest)
        return 0;
    t = (float)(v - rest) / (float)(end - rest);
    if (t <= dead)
        return 0;
    return DGK_MIN(1.0f, (t - dead) / (1.0f - dead));
}

void joymap_read(const joymap *m, float *steer, float *accel, float *brake)
{
    const int16_t *a = dgk_app.joy_axis;
    *steer = travel(a[m->steer_axis], m->steer_rest, m->steer_left, m->deadzone) -
             travel(a[m->steer_axis], m->steer_rest, m->steer_right, m->deadzone);
    *accel = travel(a[m->accel_axis], m->accel_rest, m->accel_full, m->deadzone);
    *brake = travel(a[m->brake_axis], m->brake_rest, m->brake_full, m->deadzone);
}

/* ---- calibration ---------------------------------------------------------- */

static const char *const step_name[] = { "off", "rest", "left", "right", "accel", "brake", "done" };

static void next(calib *c, int step)
{
    c->step = step;
    c->step_tick = dgk_app.ticks;
    c->axis = -1;
    c->extreme = 0;
    dgk_log("FW-CAL %s", step_name[step]);
}

void calib_start(calib *c)
{
    joymap_default(&c->result);
    next(c, CAL_REST);
}

const char *calib_prompt(const calib *c)
{
    switch (c->step) {
    case CAL_REST:
        return "LET GO OF THE STICK AND PEDALS";
    case CAL_LEFT:
        return "STEER FULL LEFT, THEN LET GO";
    case CAL_RIGHT:
        return "STEER FULL RIGHT, THEN LET GO";
    case CAL_ACCEL:
        return "ACCELERATE FULLY, THEN LET GO";
    case CAL_BRAKE:
        return "BRAKE FULLY, THEN LET GO";
    case CAL_DONE:
        return "SAVED. HAPPY TRUCKING!";
    default:
        return "";
    }
}

/* In this step: an axis (other than skip, and not `avoid` in the direction
 * avoid_sign) moved far, and now back to rest? Then *axis and *extreme. */
static int moved_and_back(calib *c, int only, int skip, int avoid, int avoid_sign)
{
    int a;
    if (c->axis < 0) {
        int best = -1, far = MOVED;
        for (a = 0; a < DGK_JOY_AXES; a++) {
            int d = dgk_app.joy_axis[a] - c->rest[a];
            if ((only >= 0 && a != only) || a == skip || (a == avoid && (d > 0) == (avoid_sign > 0)))
                continue;
            if (abs(d) > far) {
                far = abs(d);
                best = a;
            }
        }
        if (best >= 0) {
            c->axis = best;
            c->extreme = dgk_app.joy_axis[best];
        }
        return 0;
    }
    a = c->axis;
    if (abs(dgk_app.joy_axis[a] - c->rest[a]) > abs(c->extreme - c->rest[a]))
        c->extreme = dgk_app.joy_axis[a];
    return abs(dgk_app.joy_axis[a] - c->rest[a]) < BACK;
}

int calib_tick(calib *c)
{
    joymap *m = &c->result;
    int a;
    switch (c->step) {
    case CAL_REST:                                   /* a second of letting go, then the rest positions */
        if (dgk_app.ticks - c->step_tick >= DGK_TICK_HZ) {
            for (a = 0; a < DGK_JOY_AXES; a++)
                c->rest[a] = dgk_app.joy_axis[a];
            dgk_log("FW-CAL rest at %d %d %d %d", c->rest[0], c->rest[1], c->rest[2], c->rest[3]);
            next(c, CAL_LEFT);
        }
        break;
    case CAL_LEFT:
        if (moved_and_back(c, -1, -1, -1, 0)) {
            m->steer_axis = c->axis;
            m->steer_rest = c->rest[c->axis];
            m->steer_left = c->extreme;
            dgk_log("FW-CAL left: axis %d to %d", c->axis, c->extreme);
            next(c, CAL_RIGHT);
        }
        break;
    case CAL_RIGHT:                                  /* the same axis, the other way */
        if (moved_and_back(c, m->steer_axis, -1, m->steer_axis, m->steer_left - m->steer_rest)) {
            m->steer_right = c->extreme;
            dgk_log("FW-CAL right: axis %d to %d", c->axis, c->extreme);
            next(c, CAL_ACCEL);
        }
        break;
    case CAL_ACCEL:
        if (moved_and_back(c, -1, m->steer_axis, -1, 0)) {
            m->accel_axis = c->axis;
            m->accel_rest = c->rest[c->axis];
            m->accel_full = c->extreme;
            dgk_log("FW-CAL accel: axis %d to %d", c->axis, c->extreme);
            next(c, CAL_BRAKE);
        }
        break;
    case CAL_BRAKE:                                  /* any axis but steering, not the accelerator's way */
        if (moved_and_back(c, -1, m->steer_axis, m->accel_axis, m->accel_full - m->accel_rest)) {
            m->brake_axis = c->axis;
            m->brake_rest = c->rest[c->axis];
            m->brake_full = c->extreme;
            m->calibrated = 1;
            dgk_log("FW-CAL brake: axis %d to %d", c->axis, c->extreme);
            next(c, CAL_DONE);
        }
        break;
    case CAL_DONE:
        return dgk_app.ticks - c->step_tick >= DGK_TICK_HZ;
    default:
        break;
    }
    return 0;
}
