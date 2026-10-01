/* input.h - a joystick or wheel (with pedals or not) as steering, an
 * accelerator and a brake, mapped by the calibration screen and kept in
 * the settings file; and the calibration screen itself.
 *
 * Calibration asks for the controls at rest, then full left, full right,
 * the accelerator and the brake. Each step waits for an axis to move well
 * away from rest and come back: whichever axis moves is the one used, so a
 * stick (forward to accelerate, back to brake) and a wheel with separate
 * pedals both work, whatever their axes' order or direction. */
#ifndef FW_INPUT_H
#define FW_INPUT_H

#include "dgk/app.h"
#include "dgk/cfg.h"

typedef struct joymap {
    int steer_axis, steer_rest, steer_left, steer_right;
    int accel_axis, accel_rest, accel_full;
    int brake_axis, brake_rest, brake_full;
    int deadzone;                   /* thousandths of the travel */
    int handbrake_button, horn_button;
    int calibrated;
} joymap;

void joymap_default(joymap *m);     /* a stick: axis 0 steers, axis 1 forward accelerates, back brakes */
void joymap_load(joymap *m, const dgk_cfg *c);
void joymap_store(const joymap *m, dgk_cfg *c);
/* The joystick's steering (-1 full right .. +1 full left), accelerator and brake (0..1). */
void joymap_read(const joymap *m, float *steer, float *accel, float *brake);

enum { CAL_OFF, CAL_REST, CAL_LEFT, CAL_RIGHT, CAL_ACCEL, CAL_BRAKE, CAL_DONE };

typedef struct calib {
    int step;
    uint32_t step_tick;             /* dgk_app.ticks when the step began */
    int axis, extreme;              /* the axis seen moving in this step (-1: none yet), its farthest value */
    int rest[DGK_JOY_AXES];
    joymap result;
} calib;

void calib_start(calib *c);
/* One tick of the calibration screen; 1 once it has finished (c->result). */
int  calib_tick(calib *c);
const char *calib_prompt(const calib *c);

#endif
