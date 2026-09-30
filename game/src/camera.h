/* camera.h - the angled follow camera: 64 degrees down, behind the
 * cab, turning with the lorry through a damped spring, centred on the
 * whole rig and looking further ahead with speed. */
#ifndef FW_CAMERA_H
#define FW_CAMERA_H

#include "vehicle.h"

typedef struct camera {
    float yaw, yaw_rate;             /* spring state */
    float tx, ty;                    /* where it looks, on the ground */
    float dist;
} camera;

void camera_reset(camera *c, const rig *r);
void camera_tick(camera *c, const rig *r, float dt);
void camera_apply(const camera *c, const camera *prev, float alpha, float aspect, float ground);

#endif
