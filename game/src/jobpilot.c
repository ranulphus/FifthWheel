/* jobpilot.c - the autopilot for a whole job (see jobpilot.h). */
#include "jobpilot.h"
#include "dgk/log.h"
#include <math.h>
#include <string.h>

#define MAX_STEER (35.0f * WG_PI / 180.0f)

static float wrap(float a)
{
    while (a > WG_PI)
        a -= 2 * WG_PI;
    while (a < -WG_PI)
        a += 2 * WG_PI;
    return a;
}

/* A depot's frame: u along its front, v towards the warehouse. */
static void depot_point(const wg_depot *d, float u, float v, float *x, float *y)
{
    float c = cosf(d->heading), s = sinf(d->heading);
    *x = d->x + c * u - s * v;
    *y = d->y + s * u + c * v;
}

static void add(jobpilot *p, float x, float y)
{
    int n = p->npath;
    if (n == JP_PATH_MAX)
        return;
    p->path[n][0] = x;
    p->path[n][1] = y;
    p->along[n] = 0;
    if (n) {
        float dx = x - p->path[n - 1][0], dy = y - p->path[n - 1][1];
        p->along[n] = p->along[n - 1] + sqrtf(dx * dx + dy * dy);
    }
    p->npath++;
}

static void add_depot(jobpilot *p, const wg_depot *d, float u, float v)
{
    float x, y;
    depot_point(d, u, v, &x, &y);
    add(p, x, y);
}

static float turn_at(const float (*r)[2], int i, int k, float *u1, float *u2)
{
    float ax = r[i][0] - r[i - k][0], ay = r[i][1] - r[i - k][1];
    float bx = r[i + k][0] - r[i][0], by = r[i + k][1] - r[i][1];
    float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
    u1[0] = ax / la; u1[1] = ay / la;
    u2[0] = bx / lb; u2[1] = by / lb;
    return atan2f(u1[0] * u2[1] - u1[1] * u2[0], u1[0] * u2[0] + u1[1] * u2[1]);
}

/* The road points, with every sharp corner (a junction: more than 45
 * degrees over 24 m each side) rounded off by an arc: radius 7-16 m, its
 * ends at most 36 m from the corner, so a rig with a trailer can take it. */
static void add_route(jobpilot *p, const float (*r)[2], int n)
{
    const int k = 3;
    int i = 0;
    while (i < n) {
        float u1[2], u2[2], th, best = 0;
        int c, at = -1;
        /* A corner within the next few points: the sharpest of them. */
        for (c = DGK_MAX(i, k); c + k < n && c <= i + 2; c++) {
            th = fabsf(turn_at(r, c, k, u1, u2));
            if (th > 45.0f * WG_PI / 180.0f && th > best) {
                best = th;
                at = c;
            }
        }
        if (at < 0 || (at + 1 + k < n && fabsf(turn_at(r, at + 1, k, u1, u2)) > best)) {
            add(p, r[i][0], r[i][1]);
            i++;
            continue;
        }
        {
            float alpha, rad, t, sweep, o[2], t1[2], a0;
            int m, steps, side;
            th = turn_at(r, at, k, u1, u2);
            alpha = WG_PI - fabsf(th);
            rad = DGK_CLAMP(36.0f * tanf(alpha / 2), 7.0f, 16.0f);
            t = rad / tanf(alpha / 2);
            side = th > 0 ? 1 : -1;
            t1[0] = r[at][0] - u1[0] * t; t1[1] = r[at][1] - u1[1] * t;
            o[0] = t1[0] - side * u1[1] * rad;       /* the centre: left of the way in for a left turn */
            o[1] = t1[1] + side * u1[0] * rad;
            /* Drop what was already added inside the arc, then the arc. */
            while (p->npath > 0) {
                float dx = p->path[p->npath - 1][0] - r[at][0], dy = p->path[p->npath - 1][1] - r[at][1];
                if (dx * u1[0] + dy * u1[1] <= -t || dx * dx + dy * dy > (t + 10.0f) * (t + 10.0f))
                    break;
                p->npath--;
            }
            a0 = atan2f(t1[1] - o[1], t1[0] - o[0]);
            sweep = th;
            steps = (int)(fabsf(sweep) * rad / 3.0f) + 2;
            for (m = 0; m <= steps; m++) {
                float a = a0 + sweep * m / steps;
                add(p, o[0] + rad * cosf(a), o[1] + rad * sinf(a));
            }
            /* On past the arc's end. */
            for (i = at; i < n && (r[i][0] - r[at][0]) * u2[0] + (r[i][1] - r[at][1]) * u2[1] < t + 1.0f; i++)
                continue;
        }
    }
}

void jobpilot_reset(jobpilot *p, const jobs *j)
{
    memset(p, 0, sizeof *p);
    p->delivered = j->delivered;
}

void jobpilot_start(const jobs *j, int depot, float *x, float *y, float *heading)
{
    const parked *t = &j->trailers[depot];
    *heading = t->heading;
    *x = t->x + 9.0f * cosf(t->heading);
    *y = t->y + 9.0f * sinf(t->heading);
}

/* Coupled at the waiting trailer (the apron's left, facing +u): ahead, a
 * right turn onto the gate road, out through the gate; the road; in
 * through the far gate, across to the right of the bay, a loop left round
 * to face the gate on the bay's line, and ahead a little so the trailer
 * lines up behind. */
static void build_path(jobpilot *p, const jobs *j, const world *w)
{
    const wg_depot *a = &w->depots[j->current.from], *b = &w->depots[j->current.to];
    float ub = (j->current.bay - 1) * 16.0f;
    int i;
    p->npath = p->next = 0;
    add_depot(p, a, -16, -22);
    for (i = 0; i <= 6; i++) {                       /* radius 12 about (-12, -34) */
        float t = WG_PI / 2 - i * WG_PI / 12;
        add_depot(p, a, -12 + 12 * cosf(t), -34 + 12 * sinf(t));
    }
    add_depot(p, a, 0, -44);
    add_route(p, (const float (*)[2])j->route, j->nroute);
    add_depot(p, b, 0, -44);
    add_depot(p, b, 0, -38);
    add_depot(p, b, ub + 24, -26);
    add_depot(p, b, ub + 24, -8);
    for (i = 1; i <= 12; i++) {                      /* radius 12 about (ub + 12, -8) */
        float t = i * WG_PI / 12;
        add_depot(p, b, ub + 12 + 12 * cosf(t), -8 + 12 * sinf(t));
    }
    for (i = 1; i <= 3; i++)
        add_depot(p, b, ub, -8 - i * 5.0f);
}

/* Pedals for a wanted speed (negative: backwards), by the game's arcade
 * rules: the brake held at a standstill selects reverse, the accelerator
 * held selects first; reversing, the brake pedal drives and the
 * accelerator brakes. */
static void pedals(const rig *r, float want, rig_input *in)
{
    in->accel = in->decel = 0;
    if (want >= 0) {
        if (r->gear < 0) {
            in->accel = fabsf(r->v) > 0.05f ? 0.8f : 1.0f;
            return;
        }
        if (r->v < want - 0.3f)
            in->accel = DGK_CLAMP((want - r->v) * 0.35f, 0.08f, 1.0f);
        else if (r->v > want + 0.8f)
            in->decel = DGK_CLAMP((r->v - want) * 0.25f, 0.1f, 0.8f);
        return;
    }
    if (r->gear > 0) {
        in->decel = 1.0f;
        return;
    }
    if (r->v > want + 0.1f)
        in->decel = DGK_CLAMP((r->v - want) * 0.6f, 0.05f, 0.5f);
    else if (r->v < want - 0.25f)
        in->accel = DGK_CLAMP((want - r->v) * 0.8f, 0.1f, 0.45f);
}

/* Stop and stay in gear: the pedal that brakes, below the level that
 * would change direction. */
static void hold(const rig *r, rig_input *in)
{
    in->accel = r->gear < 0 ? 0.4f : 0.0f;
    in->decel = r->gear > 0 ? 0.4f : 0.0f;
}

static float steer_to_input(float delta, const rig *r)
{
    float max = MAX_STEER / (1.0f + fabsf(r->v) / 15.0f);
    return DGK_CLAMP(delta / max, -1.0f, 1.0f);
}

/* Forward pure pursuit along the path; returns the metres left. */
static float drive_path(jobpilot *p, const rig *r, rig_input *in)
{
    float look = 9.0f + 0.5f * fabsf(r->v), dx, dy, alpha, want, left;
    int target, k, m;
    /* Past a point: beyond the line through it across the path, and near it. */
    while (p->next < p->npath - 1) {
        float sx = p->path[p->next + 1][0] - p->path[p->next][0], sy = p->path[p->next + 1][1] - p->path[p->next][1];
        dx = r->x - p->path[p->next][0];
        dy = r->y - p->path[p->next][1];
        if (dx * dx + dy * dy > 20.0f * 20.0f || (dx * sx + dy * sy < 0 && dx * dx + dy * dy > 1.0f))
            break;
        p->next++;
    }
    for (target = p->next; target < p->npath - 1; target++) {
        dx = p->path[target][0] - r->x;
        dy = p->path[target][1] - r->y;
        if (dx * dx + dy * dy >= look * look)
            break;
    }
    dx = p->path[target][0] - r->x;
    dy = p->path[target][1] - r->y;
    alpha = wrap(atan2f(dy, dx) - r->heading);
    in->steer = steer_to_input(atanf(2.0f * RIG_WHEELBASE * sinf(alpha) / DGK_MAX(look, sqrtf(dx * dx + dy * dy))), r);
    dx = p->path[p->next][0] - r->x;
    dy = p->path[p->next][1] - r->y;
    left = p->along[p->npath - 1] - p->along[p->next] + sqrtf(dx * dx + dy * dy);
    /* Slow for bends in the next 40 m and for the end. */
    want = 14.0f;
    for (k = p->next; k + 1 < p->npath && p->along[k] < p->along[p->next] + 40.0f; k++) {
        m = k + 1;
        if (k > p->next) {
            float h0 = atan2f(p->path[k][1] - p->path[k - 1][1], p->path[k][0] - p->path[k - 1][0]);
            float h1 = atan2f(p->path[m][1] - p->path[k][1], p->path[m][0] - p->path[k][0]);
            float turn = fabsf(wrap(h1 - h0)) / DGK_MAX(p->along[m] - p->along[k - 1], 1.0f);   /* rad per metre */
            if (turn > 0.012f)
                want = DGK_MIN(want, DGK_MAX(4.0f, 14.0f - (turn - 0.012f) * 250.0f));
        }
    }
    if (left > 1.5f)
        want = DGK_MIN(want, DGK_MAX(1.0f, sqrtf(2.0f * 1.2f * (left - 1.5f))));
    else
        want = 0;
    if (want > 0)
        pedals(r, want, in);
    else
        hold(r, in);
    return left;
}

void jobpilot_drive(jobpilot *p, const jobs *j, const rig *r, const world *w, rig_input *in)
{
    in->steer = 0;
    in->accel = in->decel = 0;
    in->handbrake = 0;
    if (j->delivered != p->delivered)
        p->phase = JP_DONE;
    else if (j->state == JOB_HAULING && p->phase == JP_COUPLE) {
        build_path(p, j, w);
        p->phase = JP_DRIVE;
        dgk_log("FW-PILOT coupled: a path of %d points, %.0f m", p->npath, p->along[p->npath - 1]);
    }
    switch (p->phase) {
    case JP_COUPLE: {
        /* Back along the trailer's line: aim the rear axle at a point on
         * that line 4 m behind the fifth wheel. */
        const parked *t = &j->trailers[j->current.from];
        float c = cosf(t->heading), s = sinf(t->heading), hx, hy, along, look = 4.0f, tx, ty, alpha;
        if (j->state != JOB_TO_PICKUP) {
            hold(r, in);
            break;
        }
        rig_hitch(r, &hx, &hy);
        along = (hx - t->x) * c + (hy - t->y) * s;             /* the fifth wheel ahead of the kingpin */
        tx = t->x + c * DGK_MAX(along - look, -2.0f);
        ty = t->y + s * DGK_MAX(along - look, -2.0f);
        alpha = wrap(atan2f(ty - r->y, tx - r->x) - (r->heading + WG_PI));
        in->steer = steer_to_input(-atanf(2.0f * RIG_WHEELBASE * sinf(alpha) / look), r);
        pedals(r, along > 2.0f ? -1.0f : -0.4f, in);
        break;
    }
    case JP_DRIVE:
        if (drive_path(p, r, in) < 1.5f && fabsf(r->v) < 0.1f) {
            p->phase = JP_REVERSE;
            dgk_log("FW-PILOT staged: lat=%.2f gap=%.1f angle=%.1f articulation=%.1f", j->lat, j->gap,
                    j->angle * 180.0f / WG_PI, rig_articulation(r) * 180.0f / WG_PI);
        }
        break;
    case JP_REVERSE: {
        const float look = 8.0f, k = 1.5f;
        float bx, by, bh, ax, ay, c, s, along, aim, alpha, kappa, phi_d, phi, r_d, delta, v;
        jobs_bay(j, w, &bx, &by, &bh);
        c = cosf(bh);
        s = sinf(bh);
        rig_trailer_axle(r, &ax, &ay);
        along = (ax - bx) * c + (ay - by) * s;                 /* the axle, out from the dock face */
        aim = DGK_MAX(along - look, -3.0f);
        alpha = wrap(atan2f(by + s * aim - ay, bx + c * aim - ax) - (r->trailer_heading + WG_PI));
        kappa = 2.0f * sinf(alpha) / look;                     /* the trailer's wanted curvature, backwards */
        phi_d = DGK_CLAMP(-atanf(kappa * RIG_TRAILER_LEN), -0.6f, 0.6f);
        phi = rig_articulation(r);
        /* phi' = r (1 - e cos phi / L2) - v sin phi / L2: the yaw rate that
         * takes phi to phi_d at rate k. */
        v = r->v < -0.05f ? r->v : -0.5f;
        r_d = (k * (phi_d - phi) + v * sinf(phi) / RIG_TRAILER_LEN) / (1.0f - RIG_HITCH * cosf(phi) / RIG_TRAILER_LEN);
        delta = atanf(r_d * RIG_WHEELBASE / v);
        in->steer = steer_to_input(delta, r);
        if (j->gap > 0.3f)
            pedals(r, j->gap > 6.0f ? -1.2f : -0.5f, in);
        else
            hold(r, in);
        break;
    }
    default:
        hold(r, in);
        break;
    }
}
