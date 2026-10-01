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
static void target(const rig *r, float *x, float *y, float *dist)
{
    float ax, ay, ahead = r->v > 0 ? 0.6f * r->v : 0;
    rig_trailer_axle(r, &ax, &ay);
    *x = (r->x + 2.0f * cosf(r->heading) + ax) * 0.5f + ahead * cosf(r->heading);
    *y = (r->y + 2.0f * sinf(r->heading) + ay) * 0.5f + ahead * sinf(r->heading);
    *dist = 40.0f + 0.3f * fabsf(r->v);
}

void camera_reset(camera *c, const rig *r)
{
    c->yaw = r->heading;
    c->yaw_rate = 0;
    c->pitch = 64.0f * FW_PI / 180.0f;
    target(r, &c->tx, &c->ty, &c->dist);
}

void camera_tick(camera *c, const rig *r, float dt, const camera_focus *f)
{
    const float w = 2.5f;                      /* critically damped, about 0.4 s */
    float tx, ty, dist, k = 1.0f - expf(-4.0f * dt);
    float err = wrap((f ? f->yaw : r->heading) - c->yaw);
    c->yaw_rate += (w * w * err - 2 * w * c->yaw_rate) * dt;
    c->yaw = wrap(c->yaw + c->yaw_rate * dt);
    target(r, &tx, &ty, &dist);
    if (f) {
        tx = f->x;
        ty = f->y;
        dist = 36.0f;
    }
    c->pitch += ((f ? 75.0f : 64.0f) * FW_PI / 180.0f - c->pitch) * (1.0f - expf(-1.5f * dt));
    c->tx += (tx - c->tx) * k;
    c->ty += (ty - c->ty) * k;
    c->dist += (dist - c->dist) * k;
}

void camera_apply(const camera *c, const camera *prev, float alpha, float aspect, float ground)
{
    float pitch = prev->pitch + (c->pitch - prev->pitch) * alpha;
    float yaw = prev->yaw + wrap(c->yaw - prev->yaw) * alpha;
    float tx = prev->tx + (c->tx - prev->tx) * alpha, ty = prev->ty + (c->ty - prev->ty) * alpha;
    float dist = prev->dist + (c->dist - prev->dist) * alpha;
    dgk_v3 at, eye, up = { 0, 1, 0 };
    at.x = tx;
    at.y = ground + 1.0f;
    at.z = -ty;
    eye.x = tx - dist * cosf(pitch) * cosf(yaw);
    eye.y = ground + dist * sinf(pitch);
    eye.z = -(ty - dist * cosf(pitch) * sinf(yaw));
    dgk_gfx_perspective(42.0f, aspect, 8.0f, 140.0f);
    dgk_gfx_look_at(eye, at, up);
}
