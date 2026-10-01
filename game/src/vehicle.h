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
#define RIG_TRAILER_REAR 12.6f  /* kingpin to the trailer's back */
#define RIG_TRACTOR_MASS 8000.0f

typedef struct rig_input {
    float steer;                /* -1 full right .. +1 full left */
    float accel, decel;         /* pedals 0..1 */
    int handbrake;
} rig_input;

enum { TRAILER_BOX, TRAILER_FLATBED, TRAILER_TANKER, TRAILER_TYPES };

typedef struct rig {
    int has_trailer;            /* 0: the tractor alone (bobtail) */
    int trailer_type;
    float trailer_mass;         /* kg, with its load */
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

void rig_init(rig *r, float x, float y, float heading);   /* with a box trailer, half loaded */
/* Take up a trailer whose kingpin is at (kx, ky), facing `heading`; drop it
 * (its kingpin and heading returned). */
void rig_couple(rig *r, int type, float mass, float heading);
void rig_uncouple(rig *r, float *kx, float *ky, float *heading);
void rig_trailer_rear(const rig *r, float *rx, float *ry);      /* the middle of its back */
/* A trailer standing alone with its kingpin at (kx, ky): its outline from
 * 2 m behind the kingpin (room for a tractor backing under it) to its back. */
void rig_parked_box(float kx, float ky, float heading, obb *b);
void rig_step(rig *r, const rig_input *in, float dt, const world *w);
/* Where the back corners go if the rig reverses `metres` with the front
 * wheels held where they are: n + 1 points each, from where they are now
 * (the trailer's back, or the tractor's without one). The same kinematics
 * as rig_step, without collisions: the reversing camera's guide lines. */
void rig_predict_reverse(const rig *r, float metres, int n, float (*left)[2], float (*right)[2]);
void rig_hitch(const rig *r, float *hx, float *hy);           /* the fifth wheel */
void rig_trailer_axle(const rig *r, float *ax, float *ay);
float rig_articulation(const rig *r);                         /* tractor minus trailer heading */
uint32_t rig_hash(const rig *r, uint32_t h);

#endif
