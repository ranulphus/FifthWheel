/* autopilot.c - pure pursuit round the path (see autopilot.h). */
#include "autopilot.h"
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

void autopilot_reset(autopilot *a, const rig *r, const world *w)
{
    int i, best = 0;
    float bd = 1e30f;
    for (i = 0; i < w->npath; i++) {
        float dx = w->path[i * 2] - r->x, dy = w->path[i * 2 + 1] - r->y, d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    a->next = (best + 1) % w->npath;
    a->laps = a->half = 0;
    a->speed = 14.0f;
}

void autopilot_drive(autopilot *a, const rig *r, const world *w, rig_input *in)
{
    float look = 10.0f + 0.6f * fabsf(r->v), dx = 0, dy = 0, alpha, curve, want;
    int i, n = w->npath, target = a->next, prev = a->next;
    /* Advance past the points the rig has reached. */
    for (i = 0; i < n; i++) {
        dx = w->path[a->next * 2] - r->x;
        dy = w->path[a->next * 2 + 1] - r->y;
        if (dx * cosf(r->heading) + dy * sinf(r->heading) > 2.0f)
            break;
        a->next = (a->next + 1) % n;
    }
    if (a->next < prev) {                        /* wrapped past the start */
        if (a->half)
            a->laps++;
        a->half = 0;
    }
    if (a->next > n / 2)
        a->half = 1;
    /* Pure pursuit: the first path point at least `look` metres away. */
    for (i = 0, target = a->next; i < n; i++, target = (target + 1) % n) {
        dx = w->path[target * 2] - r->x;
        dy = w->path[target * 2 + 1] - r->y;
        if (dx * dx + dy * dy >= look * look)
            break;
    }
    alpha = wrap(atan2f(dy, dx) - r->heading);
    in->steer = atanf(2.0f * RIG_WHEELBASE * sinf(alpha) / look) / (35.0f * FW_PI / 180.0f / (1.0f + fabsf(r->v) / 15.0f));
    in->steer = DGK_CLAMP(in->steer, -1.0f, 1.0f);
    /* Slow for the bends: how much the path turns over the next 40 m. */
    {
        int j = (a->next + 10) % n;
        float h0 = atan2f(w->path[((a->next + 1) % n) * 2 + 1] - w->path[a->next * 2 + 1],
                          w->path[((a->next + 1) % n) * 2] - w->path[a->next * 2]);
        float h1 = atan2f(w->path[((j + 1) % n) * 2 + 1] - w->path[j * 2 + 1],
                          w->path[((j + 1) % n) * 2] - w->path[j * 2]);
        curve = fabsf(wrap(h1 - h0));
    }
    want = curve > 0.3f ? 9.0f : a->speed;
    in->accel = r->v < want - 0.5f ? DGK_CLAMP((want - r->v) * 0.4f, 0.0f, 1.0f) : 0;
    in->decel = r->v > want + 1.5f ? DGK_CLAMP((r->v - want) * 0.2f, 0.0f, 0.6f) : 0;
    in->handbrake = 0;
}
