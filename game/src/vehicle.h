/* vehicle.h - the articulated lorry: a tractor and a semi-trailer on the
 * ground plane (x east, y north; headings anticlockwise from east).
 *
 * The tractor is a kinematic bicycle (yaw rate v tan(steer) / wheelbase),
 * its turn limited by tyre grip at speed; the trailer turns about the
 * fifth wheel with the exact kinematics of a kingpin behind (or ahead of)
 * the drive axle, so reversing is as unstable as the real thing. Past the
 * articulation limit the rig jackknifes (a damage event). Longitudinal: a
 * diesel's torque curve through a six-speed automatic box with a shift
 * pause, traction, brakes, rolling and air resistance. Arcade pedals: the
 * brake held at a standstill selects reverse and then drives backwards. */
#ifndef FW_VEHICLE_H
#define FW_VEHICLE_H

#include "world.h"

#define RIG_WHEELBASE 3.8f      /* front axle ahead of the rear (drive) axle */
#define RIG_HITCH 0.4f          /* fifth wheel ahead of the rear axle */
#define RIG_TRAILER_LEN 9.0f    /* kingpin to the trailer's axle group */

typedef struct rig_input {
    float steer;                /* -1 full right .. +1 full left */
    float accel, decel;         /* pedals 0..1 */
    int handbrake;
} rig_input;

typedef struct rig {
    float x, y, heading;        /* the tractor's rear axle */
    float v;                    /* along the heading, m/s (negative: reversing) */
    float steer;                /* front wheel angle, radians */
    float yaw_rate;
    float trailer_heading;
    int gear;                   /* -1 reverse, 1..6 */
    float rpm, throttle, brake;
    float shift_timer;          /* torque cut while > 0 */
    float reverse_timer;        /* the brake held at a standstill */
    float damage;               /* 0..100 */
    int jackknifed, jackknifes, hits;
    float brake_held;           /* seconds the brake has been on (air hiss on release) */
    int air_release;            /* set for one step when the brakes let go */
    int shifted;                /* +1 up / -1 down this step */
} rig;

void rig_init(rig *r, float x, float y, float heading);
void rig_step(rig *r, const rig_input *in, float dt, const world *w);
void rig_hitch(const rig *r, float *hx, float *hy);           /* the fifth wheel */
void rig_trailer_axle(const rig *r, float *ax, float *ay);
float rig_articulation(const rig *r);                         /* tractor minus trailer heading */
uint32_t rig_hash(const rig *r, uint32_t h);

#endif
