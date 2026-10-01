/* camera.h - the angled follow camera: 64 degrees down, behind the
 * cab, turning with the lorry through a damped spring, centred on the
 * whole rig and looking further ahead with speed. For coupling and docking
 * it is given a focus: steeper (75 degrees), closer, looking at a point
 * the way the rig is backing (the trailer's nose, the bay). */
#ifndef FW_CAMERA_H
#define FW_CAMERA_H

#include "vehicle.h"

typedef struct camera {
    float yaw, yaw_rate;             /* spring state */
    float tx, ty;                    /* where it looks, on the ground */
    float dist;
    float pitch;                     /* radians down from level */
} camera;

typedef struct camera_focus {
    float x, y, yaw;                 /* where to look, and from which way */
} camera_focus;

void camera_reset(camera *c, const rig *r);
void camera_tick(camera *c, const rig *r, float dt, const camera_focus *f);   /* f: NULL to follow */
void camera_apply(const camera *c, const camera *prev, float alpha, float aspect, float ground);

#endif
