/* camera.h - the angled follow camera: 64 degrees down when manoeuvring,
 * coming down towards the detail's pitch_min as the rig drives faster (the
 * road ahead opens up; low enough, the sky), behind the cab, turning with
 * the lorry through a damped spring, centred on the whole rig and looking
 * further ahead with speed. For coupling and docking it is given a focus:
 * steeper (75 degrees), closer, looking at a point the way the rig is
 * backing (the trailer's nose, the bay). */
#ifndef FW_CAMERA_H
#define FW_CAMERA_H

#include "vehicle.h"

typedef struct camera {
    float yaw, yaw_rate;             /* spring state */
    float tx, ty;                    /* where it looks, on the ground */
    float dist;
    float pitch;                     /* radians down from level */
    float pitch_min;                 /* radians: the lowest it comes at speed (the detail's; 0: 64 degrees) */
    float lookahead;                 /* seconds of travel it aims ahead at speed (the detail's; 0: 0.6) */
    int zoom;                        /* 0 near, 1 normal, 2 far */
} camera;

typedef struct camera_focus {
    float x, y, yaw;                 /* where to look, and from which way */
} camera_focus;

void camera_reset(camera *c, const rig *r);
void camera_tick(camera *c, const rig *r, float dt, const camera_focus *f);   /* f: NULL to follow */
/* Set the matrices for the pose between prev and c; the view's pitch
 * (radians down from level, from the eye to what it looks at) and the eye's
 * height above the ground go to *view_pitch and *eye_height. */
void camera_apply(const camera *c, const camera *prev, float alpha, float aspect, float ground, float far,
                  float *view_pitch, float *eye_height);

#define CAMERA_FOVY 42.0f            /* degrees, top to bottom */

#endif
