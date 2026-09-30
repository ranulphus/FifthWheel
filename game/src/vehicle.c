/* vehicle.c - the articulated lorry (see vehicle.h). */
#include "vehicle.h"
#include <math.h>
#include <string.h>

#define FW_PI 3.14159265f
#define G 9.81f
#define MASS 20000.0f           /* tractor 8 t, trailer half loaded */
#define DRIVE_LOAD 6500.0f      /* kg on the drive axle */
#define WHEEL_R 0.52f
#define IDLE 600.0f
#define REDLINE 2200.0f
#define LAT_GRIP 5.0f           /* m/s^2 of turning before the tyres give */
#define JACKKNIFE (80.0f * FW_PI / 180.0f)

static const float gear_ratio[7] = { 50.0f, 54.0f, 30.9f, 17.6f, 10.1f, 5.76f, 3.29f };  /* [0] reverse */

/* A 13-litre diesel, Nm against rpm. */
static float torque(float rpm)
{
    static const float at[][2] = { { 600, 1100 }, { 1000, 1900 }, { 1300, 2100 }, { 1600, 2000 },
                                   { 1900, 1750 }, { 2200, 1300 } };
    int i;
    if (rpm <= at[0][0])
        return at[0][1];
    for (i = 1; i < 6; i++)
        if (rpm <= at[i][0])
            return at[i - 1][1] + (at[i][1] - at[i - 1][1]) * (rpm - at[i - 1][0]) / (at[i][0] - at[i - 1][0]);
    return 0;
}

static float wrap(float a)
{
    while (a > FW_PI)
        a -= 2 * FW_PI;
    while (a < -FW_PI)
        a += 2 * FW_PI;
    return a;
}

void rig_init(rig *r, float x, float y, float heading)
{
    memset(r, 0, sizeof *r);
    r->x = x;
    r->y = y;
    r->heading = r->trailer_heading = heading;
    r->gear = 1;
    r->rpm = IDLE;
}

void rig_hitch(const rig *r, float *hx, float *hy)
{
    *hx = r->x + RIG_HITCH * cosf(r->heading);
    *hy = r->y + RIG_HITCH * sinf(r->heading);
}

void rig_trailer_axle(const rig *r, float *ax, float *ay)
{
    float hx, hy;
    rig_hitch(r, &hx, &hy);
    *ax = hx - RIG_TRAILER_LEN * cosf(r->trailer_heading);
    *ay = hy - RIG_TRAILER_LEN * sinf(r->trailer_heading);
}

float rig_articulation(const rig *r)
{
    return wrap(r->heading - r->trailer_heading);
}

static void tractor_box(const rig *r, obb *b)
{
    b->angle = r->heading;
    b->hl = 3.0f;
    b->hw = 1.25f;
    b->cx = r->x + 1.9f * cosf(r->heading);      /* bumper 4.9 m ahead of the rear axle, back 1.1 m behind */
    b->cy = r->y + 1.9f * sinf(r->heading);
}

static void trailer_box(const rig *r, obb *b)
{
    float hx, hy;
    rig_hitch(r, &hx, &hy);
    b->angle = r->trailer_heading;
    b->hl = 6.8f;
    b->hw = 1.28f;
    b->cx = hx - 5.8f * cosf(r->trailer_heading); /* nose 1.0 m ahead of the kingpin, 13.6 m long */
    b->cy = hy - 5.8f * sinf(r->trailer_heading);
}

/* Pedals to throttle and brake, choosing forward or reverse (arcade). */
static void pedals(rig *r, const rig_input *in, float dt)
{
    float fwd = in->accel, back = in->decel;
    r->throttle = r->brake = 0;
    if (r->gear > 0) {
        r->throttle = fwd;
        r->brake = back;
        if (fabsf(r->v) < 0.3f && back > 0.5f && fwd < 0.1f) {
            r->reverse_timer += dt;
            if (r->reverse_timer > 0.35f) {
                r->gear = -1;
                r->reverse_timer = 0;
            }
        } else
            r->reverse_timer = 0;
    } else {
        r->throttle = back;
        r->brake = fwd;
        if (fabsf(r->v) < 0.3f && fwd > 0.5f && back < 0.1f) {
            r->reverse_timer += dt;
            if (r->reverse_timer > 0.2f) {
                r->gear = 1;
                r->reverse_timer = 0;
            }
        } else
            r->reverse_timer = 0;
    }
    if (in->handbrake)
        r->brake = 1;
}

static void drivetrain(rig *r, float dt, float *force)
{
    float ratio = gear_ratio[r->gear < 0 ? 0 : r->gear];
    float wheel_rpm = fabsf(r->v) / WHEEL_R * 60.0f / (2 * FW_PI);
    float engaged_rpm = wheel_rpm * ratio, f;
    r->shifted = 0;
    if (r->shift_timer > 0)
        r->shift_timer -= dt;
    if (engaged_rpm < IDLE + r->throttle * 700.0f && fabsf(r->v) < 3.0f)
        engaged_rpm = IDLE + r->throttle * 700.0f;           /* the clutch slips away from a standstill */
    if (r->shift_timer > 0)
        engaged_rpm = r->rpm + (IDLE + 200.0f - r->rpm) * DGK_MIN(1.0f, dt * 6.0f);   /* revs drop in the shift */
    r->rpm = DGK_CLAMP(engaged_rpm, IDLE, REDLINE);
    /* The automatic box. */
    if (r->gear > 0 && r->shift_timer <= 0) {
        if (wheel_rpm * ratio > 1900.0f && r->gear < 6 && r->throttle > 0.1f) {
            r->gear++;
            r->shift_timer = 0.4f;
            r->shifted = 1;
        } else if (wheel_rpm * ratio < 1050.0f && r->gear > 1) {
            r->gear--;
            r->shift_timer = 0.3f;
            r->shifted = -1;
        }
    }
    f = r->shift_timer > 0 ? 0 : torque(r->rpm) * ratio * 0.9f / WHEEL_R * r->throttle;
    if (r->rpm >= REDLINE)
        f = 0;
    f = DGK_MIN(f, 0.8f * DRIVE_LOAD * G);               /* traction */
    *force = r->gear < 0 ? -f : f;
}

static void collide(rig *r, const world *w)
{
    obb t, tr;
    int i;
    tractor_box(r, &t);
    trailer_box(r, &tr);
    for (i = 0; i < w->nboxes; i++) {
        float mx, my;
        if (obb_overlap(&t, &w->boxes[i], &mx, &my)) {
            float into = r->v * (cosf(r->heading) * mx + sinf(r->heading) * my);
            r->x += mx;
            r->y += my;
            if (into < 0) {                              /* driving into it: a bump and a bounce */
                r->damage = DGK_MIN(100.0f, r->damage + fabsf(r->v) * 1.5f);
                r->v = -0.25f * r->v;
                r->hits++;
            }
            tractor_box(r, &t);
        }
        if (obb_overlap(&tr, &w->boxes[i], &mx, &my)) {
            /* The trailer is pushed around its kingpin: turn it the way that
             * moves its middle along the push, and stop a rig backing into it. */
            float hx, hy, ox, oy, turn;
            rig_hitch(r, &hx, &hy);
            ox = tr.cx - hx;
            oy = tr.cy - hy;
            turn = (ox * my - oy * mx) / (ox * ox + oy * oy);
            r->trailer_heading = wrap(r->trailer_heading + turn);
            if (r->v < 0) {
                r->damage = DGK_MIN(100.0f, r->damage + fabsf(r->v));
                r->v = 0;
                r->hits++;
            }
            trailer_box(r, &tr);
        }
    }
}

void rig_step(rig *r, const rig_input *in, float dt, const world *w)
{
    const int substeps = 2;
    float h = dt / substeps, target, force, resist, phi, max_steer, omega_t;
    int s, braking_before = r->brake > 0.2f;
    r->air_release = 0;
    pedals(r, in, dt);
    drivetrain(r, dt, &force);
    if (r->brake > 0.2f)
        r->brake_held += dt;
    else {
        if (braking_before && r->brake_held > 0.5f)
            r->air_release = 1;
        r->brake_held = 0;
    }
    /* Steering: speed-sensitive, the wheels turning at a finite rate. */
    max_steer = 35.0f * FW_PI / 180.0f / (1.0f + fabsf(r->v) / 15.0f);
    target = DGK_CLAMP(in->steer, -1.0f, 1.0f) * max_steer;
    r->steer += DGK_CLAMP(target - r->steer, -1.2f * dt, 1.2f * dt);
    for (s = 0; s < substeps; s++) {
        float brake_f = r->brake * 0.6f * MASS * G, dv;
        resist = 0.007f * MASS * G + 0.5f * 1.2f * 6.0f * r->v * r->v;
        dv = force / MASS * h;
        /* Resistance and brakes oppose the motion and cannot reverse it. */
        {
            float stop = (resist + brake_f) / MASS * h;
            if (r->v > 0)
                r->v = DGK_MAX(0.0f, r->v + dv - stop);
            else if (r->v < 0)
                r->v = DGK_MIN(0.0f, r->v + dv + stop);
            else if (fabsf(dv) * MASS / h > resist + brake_f)
                r->v = dv > 0 ? dv - stop : dv + stop;
        }
        r->yaw_rate = r->v * tanf(r->steer) / RIG_WHEELBASE;
        if (fabsf(r->v) > 1.0f)                         /* the tyres give before the turn tightens further */
            r->yaw_rate = DGK_CLAMP(r->yaw_rate, -LAT_GRIP / fabsf(r->v), LAT_GRIP / fabsf(r->v));
        /* The trailer: its axle cannot slide sideways (kingpin kinematics).
         * Hard braking at speed locks its wheels and lets it swing. */
        phi = rig_articulation(r);
        omega_t = (r->v * sinf(phi) - RIG_HITCH * r->yaw_rate * cosf(phi)) / RIG_TRAILER_LEN;
        if (r->brake > 0.85f && fabsf(r->v) > 12.0f)
            omega_t *= 0.3f;
        r->heading = wrap(r->heading + r->yaw_rate * h);
        r->trailer_heading = wrap(r->trailer_heading + omega_t * h);
        r->x += r->v * cosf(r->heading) * h;
        r->y += r->v * sinf(r->heading) * h;
        phi = rig_articulation(r);
        if (fabsf(phi) > JACKKNIFE) {                   /* cab against the trailer */
            r->trailer_heading = wrap(r->heading - (phi > 0 ? JACKKNIFE : -JACKKNIFE));
            if (!r->jackknifed) {
                r->jackknifes++;
                r->damage = DGK_MIN(100.0f, r->damage + 5.0f + fabsf(r->v));
            }
            r->jackknifed = 1;
            r->v *= 0.9f;
        } else if (fabsf(phi) < JACKKNIFE * 0.8f)
            r->jackknifed = 0;
        if (w)
            collide(r, w);
    }
}

uint32_t rig_hash(const rig *r, uint32_t h)
{
    float f[8];
    f[0] = r->x; f[1] = r->y; f[2] = r->heading; f[3] = r->v;
    f[4] = r->trailer_heading; f[5] = r->steer; f[6] = r->rpm; f[7] = r->damage;
    h = dgk_fnv1a(h, f, sizeof f);
    return dgk_fnv1a(h, &r->gear, sizeof r->gear);
}
