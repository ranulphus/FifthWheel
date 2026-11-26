/* traffic.c - toy cars on the roads (see traffic.h). */
#include "traffic.h"
#include "lorry.h"
#include "mesh.h"
#include "dgk/log.h"
#include <GL/gl.h>
#include <math.h>
#include <string.h>

#define CRUISE 13.0f            /* m/s on the open road (47 km/h) */
#define RING_SPEED 7.0f         /* round a roundabout */
#define BRAKE 5.0f              /* m/s^2 it plans to stop with */
#define ACCEL 2.5f
#define MAX_CURV 0.22f          /* 1/m: about a 4.5 m turning circle's radius */
#define LANE_CARS 1.9f          /* how near its lane's line another car's middle is in it */
#define LANE_LORRY 2.6f         /* ... and a point down the lorry's middle (it is 2.5 m wide) */
#define STOP_R (WG_RING_R + 4.5f + 2.5f)  /* giving way at a ring: its middle this far from the town's */
#define TURN_ROUND 6.0f         /* seconds held up by the lorry before it turns round */
#define GIVE_UP 15.0f           /* ... before it goes, to come back elsewhere */

static float wrap(float a)
{
    while (a > WG_PI)
        a -= 2 * WG_PI;
    while (a < -WG_PI)
        a += 2 * WG_PI;
    return a;
}

/* ---- the roads ----------------------------------------------------------------- */

/* A road's way at a node (unit, over about 24 m): leaving it, or arriving (in). */
static void way_at(const wg_edge *e, const float *p, int at_a, int in, float *dx, float *dy)
{
    int k = DGK_MIN(3, (int)e->npoints - 1), f = (int)e->first_point, l = (int)(e->first_point + e->npoints - 1);
    int from = at_a ? f : l, to = at_a ? f + k : l - k;
    float x = p[to * 2] - p[from * 2], y = p[to * 2 + 1] - p[from * 2 + 1], len = sqrtf(x * x + y * y) + 1e-6f;
    *dx = (in ? -x : x) / len;
    *dy = (in ? -y : y) / len;
}

/* The roads the cars use: not one that leads only to a dead end (a depot,
 * or a junction whose other roads all do). Those are taken away until every
 * node left has two roads or more. */
static void find_usable(traffic *t, const wg_graph *g, const wg_edge *e)
{
    int deg[WG_MAX_NODES], i, changed = 1, ne = DGK_MIN((int)g->nedges, 64);
    memset(t->usable, 0, sizeof t->usable);
    for (i = 0; i < ne; i++)
        t->usable[i] = e[i].a < WG_MAX_NODES && e[i].b < WG_MAX_NODES;
    while (changed) {
        changed = 0;
        memset(deg, 0, sizeof deg);
        for (i = 0; i < ne; i++)
            if (t->usable[i]) {
                deg[e[i].a]++;
                deg[e[i].b]++;
            }
        for (i = 0; i < ne; i++)
            if (t->usable[i] && (deg[e[i].a] < 2 || deg[e[i].b] < 2)) {
                t->usable[i] = 0;
                changed = 1;
            }
    }
}

/* A road on from node `at`, at random among those the cars use: not `back`
 * (unless it is the only one), and at a junction not back the way it came
 * (a fork's branches meet at a narrow angle). -1: none. */
static int pick(traffic *t, const world *w, int at, int back)
{
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    const wg_graph *g = world_graph(w, &n, &e, &p);
    int list[64], count = 0, i, ne = DGK_MIN((int)g->nedges, 64);
    float ix = 0, iy = 0;
    if (back >= 0)
        way_at(&e[back], p, (int)e[back].a == at, 1, &ix, &iy);
    for (i = 0; i < ne; i++) {
        float ox, oy;
        if (!t->usable[i] || i == back || ((int)e[i].a != at && (int)e[i].b != at))
            continue;
        if (back >= 0 && n[at].kind == WG_JUNCTION) {
            way_at(&e[i], p, (int)e[i].a == at, 0, &ox, &oy);
            if (ix * ox + iy * oy < -0.5f)
                continue;
        }
        list[count++] = i;
    }
    if (!count)
        return back >= 0 && t->usable[back] ? back : -1;
    return list[dgk_rng_below(&t->rng, (uint32_t)count)];
}

/* The car's path: from node `at` along road `e` (its first `skip` points
 * left out) and on through junctions to the next town, in its left lane;
 * with `in` (the centre line's point where it came onto `at`'s ring), round
 * the ring first. */
static void build(traffic *t, const world *w, car *c, int at, int e, int skip, const float *in)
{
    static float ctr[CAR_PATH_MAX][2];
    const wg_node *nd;
    const wg_edge *ed;
    const float *p;
    int n = 0, k, node = at;
    world_graph(w, &nd, &ed, &p);
    c->ring_in = c->ring_out = -1;
    c->start_town = in ? at : -1;                    /* spawned on a road: it cannot turn back to a ring */
    c->first_edge = e;
    c->town = -1;
    if (in) {
        ctr[n][0] = in[0];
        ctr[n][1] = in[1];
        n++;
    }
    while (e >= 0 && n < CAR_PATH_MAX) {
        const wg_edge *E = &ed[e];
        int forward = (int)E->a == node, other = forward ? (int)E->b : (int)E->a;
        int q0 = forward ? (int)E->first_point : (int)(E->first_point + E->npoints - 1);
        if (n > 0 && nd[node].kind == WG_TOWN) {     /* round the ring, from where it came on to this road */
            c->ring_in = n - 1;
            n += wg_ring_arc(nd[node].x, nd[node].y, ctr[n - 1][0], ctr[n - 1][1], p[q0 * 2], p[q0 * 2 + 1], &ctr[n],
                             CAR_PATH_MAX - n);
            c->ring_out = n;
        }
        for (k = skip; k < (int)E->npoints && n < CAR_PATH_MAX; k++) {
            int q = forward ? (int)E->first_point + k : (int)(E->first_point + E->npoints - 1) - k;
            if (n > 0 && k == 0 && nd[node].kind != WG_TOWN)
                continue;                            /* a junction's point: the last road's too */
            ctr[n][0] = p[q * 2];
            ctr[n][1] = p[q * 2 + 1];
            n++;
        }
        skip = 0;
        c->last_edge = e;
        if (nd[other].kind == WG_TOWN) {
            if (n < CAR_PATH_MAX)
                c->town = other;
            break;
        }
        e = pick(t, w, other, e);
        node = other;
    }
    if (n < 2) {
        c->npath = 0;
        return;
    }
    c->end[0] = ctr[n - 1][0];
    c->end[1] = ctr[n - 1][1];
    wg_offset((const float (*)[2])ctr, n, WG_LANE, 0, c->path);
    c->npath = n;
    c->next = 1;
}

/* The car anew somewhere on a road the cars use, `away` metres or more from
 * (x, y) and 60 m from every other car; npath 0 if nowhere would do. */
static void respawn(traffic *t, const world *w, car *c, float x, float y, float away)
{
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    const wg_graph *g = world_graph(w, &n, &e, &p);
    int tries, ne = DGK_MIN((int)g->nedges, 64);
    c->npath = 0;
    for (tries = 0; tries < 80 && ne > 0; tries++) {
        int i = (int)dgk_rng_below(&t->rng, (uint32_t)ne), k, dir, q, j, ok = 1;
        float px, py;
        if (!t->usable[i] || e[i].npoints < 10)
            continue;
        k = 3 + (int)dgk_rng_below(&t->rng, e[i].npoints - 7);
        dir = (int)dgk_rng_below(&t->rng, 2);
        q = dir ? (int)(e[i].first_point + e[i].npoints - 1) - k : (int)e[i].first_point + k;
        px = p[q * 2];
        py = p[q * 2 + 1];
        if ((px - x) * (px - x) + (py - y) * (py - y) < away * away)
            continue;
        for (j = 0; j < t->ncars && ok; j++) {
            const car *o = &t->cars[j];
            ok = o == c || !o->npath || (o->x - px) * (o->x - px) + (o->y - py) * (o->y - py) > 60.0f * 60.0f;
        }
        if (!ok)
            continue;
        build(t, w, c, dir ? (int)e[i].b : (int)e[i].a, i, k, NULL);
        if (!c->npath)
            continue;
        c->x = c->px = c->path[0][0];
        c->y = c->py = c->path[0][1];
        c->heading = c->ph = atan2f(c->path[1][1] - c->path[0][1], c->path[1][0] - c->path[0][0]);
        c->v = CRUISE * 0.7f;
        c->blocked = c->knocked = c->spin = c->vx = c->vy = 0;
        c->turned = 0;
        return;
    }
}

void traffic_init(traffic *t, const world *w, float x, float y, int ncars)
{
    static const int colours = 8;
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    const wg_graph *g = world_graph(w, &n, &e, &p);
    int i;
    memset(t, 0, sizeof *t);
    dgk_rng_seed(&t->rng, 0x7A11Cu, 3);              /* the same cars every game */
    if (ncars <= 0 || !g || g->nnodes > WG_MAX_NODES)
        return;
    find_usable(t, g, e);
    t->ncars = DGK_MIN(ncars, TRAFFIC_MAX);
    for (i = 0; i < t->ncars; i++) {
        t->cars[i].colour = i % colours;
        respawn(t, w, &t->cars[i], x, y, 120.0f);
    }
}

/* ---- in its lane --------------------------------------------------------------------- */

/* Metres along a path (from (x, y) to its points from `from` on) to the
 * first of `pts` within `lateral` of it and ahead; `max` if none. *which:
 * that point's index (-1: none). */
static float lane_gap(const float (*path)[2], int npath, int from, int closed, float x, float y,
                      const float (*pts)[2], int npts, float lateral, float max, int *which)
{
    float along = 0, best = max, ax = x, ay = y;
    int i = from, steps;
    *which = -1;
    for (steps = 0; steps < npath && along < best; steps++, i++) {
        float bx, by, dx, dy, l;
        int j;
        if (i >= npath) {
            if (!closed)
                break;
            i = 0;
        }
        bx = path[i][0];
        by = path[i][1];
        dx = bx - ax;
        dy = by - ay;
        l = sqrtf(dx * dx + dy * dy);
        if (l > 1e-3f)
            for (j = 0; j < npts; j++) {
                float px = pts[j][0] - ax, py = pts[j][1] - ay, s = (px * dx + py * dy) / l;
                if (s < -0.6f || s > l + 0.6f || along + s < 0.5f || along + s >= best)
                    continue;
                if (fabsf(px * dy - py * dx) / l < lateral) {
                    best = along + s;
                    *which = j;
                }
            }
        along += l;
        ax = bx;
        ay = by;
    }
    return best;
}

float traffic_gap(const traffic *t, const float *path, int npath, int from, int closed, float x, float y, float max)
{
    float pts[TRAFFIC_MAX][2];
    int i, n = 0, which;
    for (i = 0; i < t->ncars; i++) {
        const car *c = &t->cars[i];
        if (!c->npath || (c->x - x) * (c->x - x) + (c->y - y) * (c->y - y) > (max + 10) * (max + 10))
            continue;
        pts[n][0] = c->x;
        pts[n][1] = c->y;
        n++;
    }
    if (!n || npath < 2)
        return max;
    return lane_gap((const float (*)[2])path, npath, DGK_CLAMP(from, 0, npath - 1), closed, x, y,
                    (const float (*)[2])pts, n, LANE_LORRY, max, &which);
}

/* Points down the lorry's middle, a little in from its ends: what a car
 * must not drive into. */
static int lorry_points(const rig *r, float (*pts)[2])
{
    float c = cosf(r->heading), s = sinf(r->heading), hx, hy, tc, ts;
    int n = 0, k;
    for (k = 0; k < 3; k++) {                        /* the tractor: its back, middle, bumper */
        float a = -0.9f + 2.8f * k;
        pts[n][0] = r->x + a * c;
        pts[n][1] = r->y + a * s;
        n++;
    }
    if (!r->has_trailer)
        return n;
    rig_hitch(r, &hx, &hy);
    tc = cosf(r->trailer_heading);
    ts = sinf(r->trailer_heading);
    for (k = 1; k <= 4; k++) {                       /* the trailer to its back */
        float a = -3.1f * k;
        pts[n][0] = hx + a * tc;
        pts[n][1] = hy + a * ts;
        n++;
    }
    return n;
}

/* The lorry drives into a car: knocked aside (ahead and out to the side it
 * is on), spinning; the lorry a little damaged and slowed. Touched again as
 * it slides, it is shoved on (no more damage). A car that slides into the
 * lorry stops against it. */
static void knock(traffic *t, car *c, rig *r)
{
    obb cb, tb[2];
    int k;
    cb.cx = c->x;
    cb.cy = c->y;
    cb.hl = CAR_HL;
    cb.hw = CAR_HW;
    cb.angle = c->heading;
    rig_boxes(r, &tb[0], &tb[1]);
    for (k = 0; k < (r->has_trailer ? 2 : 1); k++) {
        float mx, my, l, nx, ny, rvx, rvy, cvx, cvy, closing, hd = k ? r->trailer_heading : r->heading;
        if (!obb_overlap(&cb, &tb[k], &mx, &my))
            continue;
        c->x += mx;
        c->y += my;
        cb.cx = c->x;
        cb.cy = c->y;
        l = sqrtf(mx * mx + my * my);
        if (l < 1e-5f)
            continue;
        nx = mx / l;                                 /* from the lorry to the car */
        ny = my / l;
        rvx = r->v * cosf(hd);
        rvy = r->v * sinf(hd);
        cvx = c->knocked > 0 ? c->vx : c->v * cosf(c->heading);
        cvy = c->knocked > 0 ? c->vy : c->v * sinf(c->heading);
        closing = (rvx - cvx) * nx + (rvy - cvy) * ny;
        if (rvx * nx + rvy * ny > 0.3f && closing > 0.3f) {
            float side = (c->x - tb[k].cx) * -sinf(hd) + (c->y - tb[k].cy) * cosf(hd) > 0 ? 1.0f : -1.0f;
            c->vx = cvx + nx * closing * 1.3f - sinf(hd) * side * closing * 0.6f;
            c->vy = cvy + ny * closing * 1.3f + cosf(hd) * side * closing * 0.6f;
            if (c->knocked <= 0) {
                c->spin = side * DGK_MIN(5.0f, closing * 0.8f);
                r->damage = DGK_MIN(100.0f, r->damage + 0.5f * closing);
                r->v *= DGK_MAX(0.75f, 1.0f - 0.04f * closing);
                r->hits++;
                t->knocks++;
                dgk_log("FW-EVENT knock car=%d closing=%.1f at %.1f,%.1f (the car %.1f,%.1f heading %.0f)",
                        (int)(c - t->cars), closing, r->x, r->y, c->x, c->y, c->heading * 57.29578f);
            }
            c->knocked = 1.6f;
            c->v = 0;
        } else if (c->knocked > 0) {
            float in = c->vx * nx + c->vy * ny;
            if (in < 0) {
                c->vx -= in * nx;
                c->vy -= in * ny;
            }
        } else
            c->v = 0;
    }
}

/* Knocked: it slides and spins to a stop, then finds its lane again (or, far
 * from it, goes to come back elsewhere). */
static void slide(traffic *t, const world *w, car *c, const rig *r, float dt)
{
    float sp = sqrtf(c->vx * c->vx + c->vy * c->vy), k;
    c->x += c->vx * dt;
    c->y += c->vy * dt;
    c->heading = wrap(c->heading + c->spin * dt);
    c->spin *= 1.0f - DGK_MIN(1.0f, 2.5f * dt);
    k = sp > 1e-4f ? DGK_MAX(0.0f, sp - 7.0f * dt) / sp : 0;
    c->vx *= k;
    c->vy *= k;
    c->knocked -= dt;
    if (c->knocked <= 0 && sp < 1.5f) {
        float bd = 1e30f;
        int i, best = 0;
        for (i = 0; i < c->npath; i++) {
            float dx = c->path[i][0] - c->x, dy = c->path[i][1] - c->y;
            if (dx * dx + dy * dy < bd) {
                bd = dx * dx + dy * dy;
                best = i;
            }
        }
        c->knocked = c->vx = c->vy = c->spin = 0;
        c->v = 0;
        c->next = DGK_MIN(best + 1, c->npath - 1);
        if (bd > 20.0f * 20.0f)
            respawn(t, w, c, r->x, r->y, 250.0f);
    }
}

/* Turned round by the lorry in its way: back along its own path in the
 * other lane, to the town it came from. */
static void turn_round(car *c)
{
    static float rev[CAR_PATH_MAX][2];
    int s = c->ring_out >= 0 ? c->ring_out : 0, m = c->next - s + 1, i;
    for (i = 0; i < m; i++) {
        rev[i][0] = c->path[c->next - i][0];
        rev[i][1] = c->path[c->next - i][1];
    }
    wg_offset((const float (*)[2])rev, m, 2 * WG_LANE, 0, c->path);
    c->end[0] = (c->path[m - 1][0] + rev[m - 1][0]) * 0.5f;    /* the road's middle there */
    c->end[1] = (c->path[m - 1][1] + rev[m - 1][1]) * 0.5f;
    c->npath = m;
    c->next = 1;
    c->town = c->start_town;
    c->last_edge = c->first_edge;
    c->ring_in = c->ring_out = -1;
    c->start_town = -1;
    c->turned = 1;
    c->blocked = 0;
}

void traffic_log(const traffic *t)
{
    int i, moving = 0, active = 0;
    float sum = 0;
    for (i = 0; i < t->ncars; i++)
        if (t->cars[i].npath) {
            active++;
            sum += t->cars[i].v;
            moving += t->cars[i].v > 0.5f;
        }
    dgk_log("FW-CARS active=%d moving=%d mean_v=%.1f knocks=%d turned=%d respawned=%d waits=%d", active, moving,
            active ? sum / active : 0.0f, t->knocks, t->turned, t->respawned, t->waits);
}

void traffic_tick(traffic *t, const world *w, rig *r, float dt)
{
    const wg_node *nd;
    const wg_edge *ed;
    const float *p;
    static float pts[TRAFFIC_MAX + 8][2];
    float lorry[8][2], gap[TRAFFIC_MAX];
    int blocker[TRAFFIC_MAX], nl, i, j;
    if (!t->ncars)
        return;
    world_graph(w, &nd, &ed, &p);
    nl = lorry_points(r, lorry);
    /* Who is in whose lane ahead: the cars, then the lorry's points. */
    for (i = 0; i < t->ncars; i++) {
        car *c = &t->cars[i];
        int n = 0, which, idx[TRAFFIC_MAX + 8];
        float look = 8.0f + 2.2f * c->v;
        c->px = c->x;
        c->py = c->y;
        c->ph = c->heading;
        gap[i] = 1e9f;
        blocker[i] = -1;
        if (!c->npath || c->knocked > 0)
            continue;
        for (j = 0; j < t->ncars; j++) {
            const car *o = &t->cars[j];
            if (j == i || !o->npath || (o->x - c->x) * (o->x - c->x) + (o->y - c->y) * (o->y - c->y) > (look + 8) * (look + 8))
                continue;
            pts[n][0] = o->x;
            pts[n][1] = o->y;
            idx[n++] = j;
        }
        if (n) {
            gap[i] = lane_gap((const float (*)[2])c->path, c->npath, c->next, 0, c->x, c->y,
                              (const float (*)[2])pts, n, LANE_CARS, look, &which) - 2 * CAR_HL;
            blocker[i] = which >= 0 ? idx[which] : -1;
            if (which < 0)
                gap[i] = 1e9f;
        }
        {
            float g = lane_gap((const float (*)[2])c->path, c->npath, c->next, 0, c->x, c->y,
                               (const float (*)[2])lorry, nl, LANE_LORRY, look, &which) - CAR_HL;
            if (which >= 0 && g < gap[i]) {
                gap[i] = g;
                blocker[i] = TRAFFIC_MAX;          /* the lorry */
            }
        }
    }
    /* Two cars each in the other's way (merging at a fork): the first goes. */
    for (i = 0; i < t->ncars; i++)
        if (blocker[i] >= 0 && blocker[i] < TRAFFIC_MAX && blocker[blocker[i]] == i && i < blocker[i]) {
            gap[i] = 1e9f;
            blocker[i] = -1;
        }
    for (i = 0; i < t->ncars; i++) {
        car *c = &t->cars[i];
        float want = CRUISE, look, tx, ty, alpha, kappa, d, left = 0, dx, dy;
        int by_lorry = blocker[i] == TRAFFIC_MAX, k, target;
        if (!c->npath) {
            if (dgk_rng_below(&t->rng, 60) == 0)
                respawn(t, w, c, r->x, r->y, 250.0f);
            continue;
        }
        if (c->knocked > 0) {
            slide(t, w, c, r, dt);
            knock(t, c, r);
            continue;
        }
        /* Past the points it has reached. */
        while (c->next < c->npath - 1) {
            dx = c->path[c->next][0] - c->x;
            dy = c->path[c->next][1] - c->y;
            if (dx * cosf(c->heading) + dy * sinf(c->heading) > 0.5f && dx * dx + dy * dy > 1.0f)
                break;
            c->next++;
        }
        dx = c->path[c->next][0] - c->x;
        dy = c->path[c->next][1] - c->y;
        left = sqrtf(dx * dx + dy * dy);
        for (k = c->next; k + 1 < c->npath && left < 40.0f; k++)
            left += sqrtf((c->path[k + 1][0] - c->path[k][0]) * (c->path[k + 1][0] - c->path[k][0]) +
                          (c->path[k + 1][1] - c->path[k][1]) * (c->path[k + 1][1] - c->path[k][1]));
        /* At the path's end, its next: round the town's ring and on (or, nowhere to go, elsewhere). */
        if (c->next >= c->npath - 1 && left < 3.0f) {
            int e = c->town >= 0 ? pick(t, w, c->town, c->last_edge) : -1;
            if (e >= 0) {
                build(t, w, c, c->town, e, 0, c->end);
                c->turned = 0;
            } else
                c->npath = 0;
            if (!c->npath)
                respawn(t, w, c, r->x, r->y, 250.0f);
            continue;
        }
        /* Bends ahead, and the ring. */
        {
            float h0 = atan2f(c->path[DGK_MIN(c->next + 1, c->npath - 1)][1] - c->path[c->next][1] + 1e-6f,
                              c->path[DGK_MIN(c->next + 1, c->npath - 1)][0] - c->path[c->next][0]);
            int far = DGK_MIN(c->next + 4, c->npath - 2), on_ring;
            float h1 = atan2f(c->path[far + 1][1] - c->path[far][1] + 1e-6f, c->path[far + 1][0] - c->path[far][0]);
            float turn = fabsf(wrap(h1 - h0));
            on_ring = c->ring_in >= 0 && c->next > c->ring_in && c->next <= c->ring_out + 1;
            if (turn > 1.0f)
                want = 5.5f;
            else if (turn > 0.5f)
                want = 8.0f;
            if (on_ring)
                want = DGK_MIN(want, RING_SPEED);
        }
        /* Whatever is in its lane ahead. */
        if (gap[i] < 1e8f)
            want = DGK_MIN(want, sqrtf(2.0f * BRAKE * DGK_MAX(0.0f, gap[i] - 2.5f)));
        /* On a ring, the lorry joining it (any of it within 7 m of its next 14 m) goes first, unless it has
         * stood still a while: then it is waiting for this car. */
        if (c->ring_in >= 0 && c->next > c->ring_in && c->next <= c->ring_out && (r->v > 0.5f || c->blocked < 2.5f)) {
            int which;
            float g = lane_gap((const float (*)[2])c->path, c->npath, c->next, 0, c->x, c->y,
                               (const float (*)[2])lorry, nl, 7.0f, 14.0f + CAR_HL, &which);
            if (which >= 0) {
                want = DGK_MIN(want, sqrtf(2.0f * BRAKE * DGK_MAX(0.0f, g - CAR_HL - 2.0f)));
                by_lorry = 1;
            }
        }
        /* At the next ring, give way to what is on it: at the stop line, STOP_R from the town's middle (the
         * road comes in about straight at it); inside the line it is on its way in and goes on. */
        if (c->town >= 0 && left < 40.0f) {
            const float *ex = c->path[c->npath - 1];
            float cx = nd[c->town].x, cy = nd[c->town].y;
            float to_stop = sqrtf((c->x - cx) * (c->x - cx) + (c->y - cy) * (c->y - cy)) - STOP_R;
            int busy = to_stop < -1.0f ? -1 : 0, lorry_busy = 0;
            for (j = 0; j < t->ncars && !busy; j++) {
                const car *o = &t->cars[j];
                if (j != i && o->npath && o->ring_in >= 0 && o->next > o->ring_in && o->next <= o->ring_out + 1 &&
                    (o->x - ex[0]) * (o->x - ex[0]) + (o->y - ex[1]) * (o->y - ex[1]) < 18.0f * 18.0f)
                    busy = 1;
            }
            for (j = 0; j < nl && !busy; j++) {
                float rr = sqrtf((lorry[j][0] - cx) * (lorry[j][0] - cx) + (lorry[j][1] - cy) * (lorry[j][1] - cy));
                if (fabsf(rr - (WG_RING_R + WG_LANE)) < 3.5f &&
                    (lorry[j][0] - ex[0]) * (lorry[j][0] - ex[0]) + (lorry[j][1] - ex[1]) * (lorry[j][1] - ex[1]) <
                        18.0f * 18.0f)
                    busy = lorry_busy = 1;
            }
            if (busy > 0) {
                want = DGK_MIN(want, sqrtf(2.0f * BRAKE * DGK_MAX(0.0f, to_stop - 0.3f)));
                by_lorry |= lorry_busy;
                t->waits++;
            }
        }
        /* Held up by the lorry: turn round (off a ring), or in the end go. */
        if (by_lorry && want < 1.0f)
            c->blocked += dt;
        else
            c->blocked = 0;
        if (c->blocked > TURN_ROUND && !c->turned && c->start_town >= 0 && c->next > c->ring_out + 2 && c->next >= 3) {
            turn_round(c);
            t->turned++;
            continue;
        }
        if (c->blocked > GIVE_UP) {
            respawn(t, w, c, r->x, r->y, 250.0f);
            t->respawned++;
            continue;
        }
        /* Pure pursuit, its turn limited; the pedals. */
        look = 4.5f + 0.35f * c->v;
        for (target = c->next; target < c->npath - 1; target++) {
            dx = c->path[target][0] - c->x;
            dy = c->path[target][1] - c->y;
            if (dx * dx + dy * dy >= look * look)
                break;
        }
        tx = c->path[target][0];
        ty = c->path[target][1];
        d = sqrtf((tx - c->x) * (tx - c->x) + (ty - c->y) * (ty - c->y));
        alpha = wrap(atan2f(ty - c->y, tx - c->x) - c->heading);
        kappa = DGK_CLAMP(2.0f * sinf(alpha) / DGK_MAX(look, d), -MAX_CURV, MAX_CURV);
        if (fabsf(alpha) > WG_PI / 2) {              /* behind it (finding its lane again): turn hard, slowly */
            kappa = alpha > 0 ? MAX_CURV : -MAX_CURV;
            want = DGK_MIN(want, 3.0f);
        }
        if (c->v < want)
            c->v = DGK_MIN(want, c->v + ACCEL * dt);
        else
            c->v = DGK_MAX(want, c->v - 2.0f * BRAKE * dt);
        c->heading = wrap(c->heading + c->v * kappa * dt);
        c->x += c->v * cosf(c->heading) * dt;
        c->y += c->v * sinf(c->heading) * dt;
        knock(t, c, r);
    }
}

uint32_t traffic_hash(const traffic *t, uint32_t h)
{
    int i;
    for (i = 0; i < t->ncars; i++) {
        const car *c = &t->cars[i];
        float f[5];
        f[0] = c->x;
        f[1] = c->y;
        f[2] = c->heading;
        f[3] = c->v;
        f[4] = c->knocked;
        h = dgk_fnv1a(h, f, sizeof f);
        h = dgk_fnv1a(h, &c->next, sizeof c->next);
    }
    return dgk_fnv1a(h, &t->knocks, sizeof t->knocks);
}

/* ---- how they look --------------------------------------------------------------------- */

#define COLOURS 8
#define CV 700
#define CT 1000
static const uint32_t paint[COLOURS] = { 0xE8402Au, 0x3A6FD8u, 0xFFD23Fu, 0x5DBB3Fu,
                                         0xF28C28u, 0x8A4AB8u, 0xF06292u, 0x2BB3A8u };
static float vp[COLOURS][CV * 3], vn[COLOURS][CV * 3];
static uint8_t vc[COLOURS][CV * 4];
static uint32_t vb[COLOURS][CV];
static uint16_t vi[COLOURS][CT * 3];
static builder bld[COLOURS];
static dgk_mesh meshes[COLOURS];
static int built_quality = -1;

/* A lamp: a flat quad on the car's front (+x) or back face at x. */
static void lamp(builder *b, float x, float z0, float z1, uint32_t rgb)
{
    float p[4][3] = { { x, 0.68f, z0 }, { x, 0.68f, z1 }, { x, 0.88f, z1 }, { x, 0.88f, z0 } };
    if (x > 0)
        mb_quad_lit(b, p[0], p[3], p[2], p[1], rgb, 1.0f);
    else
        mb_quad_lit(b, p[0], p[1], p[2], p[3], rgb, 1.0f);
}

/* A toy car facing +x, its middle at the origin, on the ground (GL: y up,
 * left -z): a body, a glass cabin with a roof, four wheels; at LOW boxes,
 * the wheels a block an axle; at MEDIUM lean (6-sided wheels, flat lamps:
 * about 200 triangles), at HIGH rounder. */
static void car_mesh(builder *b, uint32_t col, int q)
{
    int s, k = q > 1 ? 2 : 1, n = q > 1 ? 10 : 6;
    float c[3];
    if (!q) {
        mb_box(b, -CAR_HL, 0.35f, -CAR_HW, CAR_HL, 1.05f, CAR_HW, col);
        mb_box(b, -1.1f, 1.05f, -0.8f, 0.9f, 1.6f, 0.8f, 0x9FD4F0u);
        mb_box(b, -1.15f, 1.6f, -0.82f, 0.95f, 1.72f, 0.82f, col);
        mb_box(b, -1.55f, 0.0f, -0.95f, -0.95f, 0.6f, 0.95f, 0x26262Cu);
        mb_box(b, 0.95f, 0.0f, -0.95f, 1.55f, 0.6f, 0.95f, 0x26262Cu);
        return;
    }
    mb_rbox(b, -CAR_HL, 0.35f, -CAR_HW, CAR_HL, 1.05f, CAR_HW, 0.3f, k, col);
    mb_rbox(b, -1.1f, 1.0f, -0.78f, 0.9f, 1.62f, 0.78f, 0.18f, k, 0x9FD4F0u);
    mb_rbox(b, -1.15f, 1.58f, -0.82f, 0.95f, 1.72f, 0.82f, 0.07f, 0, col);
    for (s = -1; s <= 1; s += 2) {
        uint32_t lit = s > 0 ? 0xFFF4C0u : 0xE8402Au;    /* lamps: head in front, tail behind */
        mb_rbox(b, s > 0 ? 1.85f : -2.1f, 0.3f, -0.92f, s > 0 ? 2.1f : -1.85f, 0.55f, 0.92f, q > 1 ? 0.1f : 0.0f,
                q > 1 ? 1 : 0, 0xD8DCE4u);
        if (q > 1) {
            mb_rbox(b, s > 0 ? 1.97f : -2.03f, 0.68f, -0.78f, s > 0 ? 2.03f : -1.97f, 0.88f, -0.46f, 0.0f, 0, lit);
            mb_rbox(b, s > 0 ? 1.97f : -2.03f, 0.68f, 0.46f, s > 0 ? 2.03f : -1.97f, 0.88f, 0.78f, 0.0f, 0, lit);
        } else {
            lamp(b, s * (CAR_HL + 0.01f), -0.78f, -0.46f, lit);
            lamp(b, s * (CAR_HL + 0.01f), 0.46f, 0.78f, lit);
        }
    }
    for (s = 0; s < 4; s++) {
        c[0] = s & 1 ? 1.25f : -1.25f;
        c[1] = 0.36f;
        c[2] = s & 2 ? 0.8f : -0.8f;
        mb_cylinder(b, 2, c, 0.36f, 0.14f, n, 0x2A2A30u, 0xE4E8F0u, s & 2 ? 1 : 2);
    }
}

void traffic_build(int quality)
{
    int i, q = DGK_CLAMP(quality, 0, 2);
    if (q == built_quality)
        return;
    built_quality = q;
    for (i = 0; i < COLOURS; i++) {
        builder *b = &bld[i];
        mb_init(b, vp[i], vc[i], vi[i], CV, CT);
        mb_normals(b, vn[i], vb[i]);
        car_mesh(b, paint[i], q);
        if (b->overflow)
            dgk_log("FW-ERROR traffic: a car's mesh is past %d vertices or %d triangles", CV, CT);
        mb_mesh(&meshes[i], b);
    }
    dgk_log("FW-CARS a car's mesh at models %d: %d triangles", q, bld[0].ntris);
}

static int pose(const car *c, float alpha, float cx, float cy, float reach, float *x, float *y, float *h)
{
    if (!c->npath)
        return 0;
    *x = c->px + (c->x - c->px) * alpha;
    *y = c->py + (c->y - c->py) * alpha;
    *h = c->ph + wrap(c->heading - c->ph) * alpha;
    return (*x - cx) * (*x - cx) + (*y - cy) * (*y - cy) < reach * reach;
}

void traffic_shadows(const traffic *t, const world *w, float alpha, float cx, float cy, float reach)
{
    int i;
    for (i = 0; i < t->ncars; i++) {
        float x, y, h;
        if (pose(&t->cars[i], alpha, cx, cy, reach, &x, &y, &h))
            lorry_shadow_footprint(w, x, y, h, -CAR_HL - 0.1f, CAR_HL + 0.1f, CAR_HW + 0.05f);
    }
}

void traffic_draw(const traffic *t, const world *w, float alpha, float cx, float cy, float reach)
{
    int i;
    if (built_quality < 0)
        traffic_build(1);
    for (i = 0; i < t->ncars; i++) {
        const car *c = &t->cars[i];
        float x, y, h, g, gf, gb;
        if (!pose(c, alpha, cx, cy, reach, &x, &y, &h))
            continue;
        g = world_height(w, x, y);
        gf = world_height(w, x + 1.3f * cosf(h), y + 1.3f * sinf(h));
        gb = world_height(w, x - 1.3f * cosf(h), y - 1.3f * sinf(h));
        glPushMatrix();
        glTranslatef(x, g, -y);
        glRotatef(h * 57.29578f, 0, 1, 0);
        glRotatef(atan2f(gf - gb, 2.6f) * 57.29578f, 0, 0, 1);
        mb_draw(&meshes[c->colour], &bld[c->colour], h);
        glPopMatrix();
    }
}
