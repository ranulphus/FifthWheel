/* camera.c - the follow camera (see camera.h). */
#include "camera.h"
#include "dgk/gfx.h"
#include <math.h>

#define FW_PI 3.14159265f

static float wrap(float a)
{
    while (a > FW_PI)
        a -= 2 * FW_PI;
    while (a < -FW_PI)
        a += 2 * FW_PI;
    return a;
}

/* Where the camera looks: the middle of the whole rig (tractor and
 * trailer), moved ahead with speed so the road ahead opens up. Reversing,
 * no look-ahead: the trailer is what the driver watches. */
static const float zoom_dist[3] = { 32.0f, 40.0f, 52.0f };

#define PITCH_HIGH (64.0f * FW_PI / 180.0f)

/* How far the camera has come down from manoeuvring (0) to its lowest (1). */
static float lowness(const camera *c, float pitch)
{
    float lo = c->pitch_min > 0 && c->pitch_min < PITCH_HIGH ? c->pitch_min : PITCH_HIGH;
    return lo < PITCH_HIGH ? DGK_CLAMP((PITCH_HIGH - pitch) / (PITCH_HIGH - lo), 0.0f, 1.0f) : 0.0f;
}

/* The pitch to drive at: high to 5 m/s (18 km/h), easing down to pitch_min
 * by 17 m/s (61 km/h); reversing, high. */
static float drive_pitch(const camera *c, const rig *r)
{
    float lo = c->pitch_min > 0 && c->pitch_min < PITCH_HIGH ? c->pitch_min : PITCH_HIGH;
    float s = DGK_CLAMP((r->v - 5.0f) / 12.0f, 0.0f, 1.0f);
    return PITCH_HIGH + (lo - PITCH_HIGH) * s * s * (3.0f - 2.0f * s);
}

static void target(const rig *r, int zoom, float *x, float *y, float *dist)
{
    float ax, ay, ahead = r->v > 0 ? 0.6f * r->v : 0;
    rig_trailer_axle(r, &ax, &ay);
    *x = (r->x + 2.0f * cosf(r->heading) + ax) * 0.5f + ahead * cosf(r->heading);
    *y = (r->y + 2.0f * sinf(r->heading) + ay) * 0.5f + ahead * sinf(r->heading);
    *dist = zoom_dist[DGK_CLAMP(zoom, 0, 2)] + 0.3f * fabsf(r->v);
}

void camera_reset(camera *c, const rig *r)
{
    c->yaw = r->heading;
    c->yaw_rate = 0;
    c->pitch = 64.0f * FW_PI / 180.0f;
    if (c->zoom < 0 || c->zoom > 2)
        c->zoom = 1;
    target(r, c->zoom, &c->tx, &c->ty, &c->dist);
}

void camera_tick(camera *c, const rig *r, float dt, const camera_focus *f)
{
    const float w = 2.5f;                      /* critically damped, about 0.4 s */
    float tx, ty, dist, k = 1.0f - expf(-4.0f * dt);
    float err = wrap((f ? f->yaw : r->heading) - c->yaw);
    c->yaw_rate += (w * w * err - 2 * w * c->yaw_rate) * dt;
    c->yaw = wrap(c->yaw + c->yaw_rate * dt);
    target(r, c->zoom, &tx, &ty, &dist);
    if (f) {
        tx = f->x;
        ty = f->y;
        dist = 36.0f;
    }
    {
        /* Up quickly (braking into a manoeuvre), down gently (speeding up). */
        float want = f ? 75.0f * FW_PI / 180.0f : drive_pitch(c, r);
        c->pitch += (want - c->pitch) * (1.0f - expf((want > c->pitch ? -1.5f : -0.8f) * dt));
    }
    /* Coming down, it is closer, so the rig stays a good size in the picture. */
    dist *= 1.0f - 0.2f * lowness(c, c->pitch);
    c->tx += (tx - c->tx) * k;
    c->ty += (ty - c->ty) * k;
    c->dist += (dist - c->dist) * k;
}

void camera_apply(const camera *c, const camera *prev, float alpha, float aspect, float ground, float far,
                  float *view_pitch, float *eye_height)
{
    float pitch = prev->pitch + (c->pitch - prev->pitch) * alpha;
    float yaw = prev->yaw + wrap(c->yaw - prev->yaw) * alpha;
    float tx = prev->tx + (c->tx - prev->tx) * alpha, ty = prev->ty + (c->ty - prev->ty) * alpha;
    float dist = prev->dist + (c->dist - prev->dist) * alpha;
    dgk_v3 at, eye, up = { 0, 1, 0 };
    at.x = tx;
    at.y = ground + 1.0f + 3.0f * lowness(c, pitch);  /* coming down, it looks a little higher: more road ahead */
    at.z = -ty;
    eye.x = tx - dist * cosf(pitch) * cosf(yaw);
    eye.y = ground + dist * sinf(pitch);
    eye.z = -(ty - dist * cosf(pitch) * sinf(yaw));
    dgk_gfx_perspective(CAMERA_FOVY, aspect, 8.0f, far);
    dgk_gfx_look_at(eye, at, up);
    *view_pitch = atan2f(eye.y - at.y, dist * cosf(pitch));
    *eye_height = eye.y - ground;
}
