/* jobs.c - delivery work (see jobs.h). */
#include "jobs.h"
#include "dgk/log.h"
#include <math.h>
#include <string.h>

const char *const grade_name[] = { "", "OK", "GOOD", "GREAT", "PERFECT" };
const char *const trailer_name[] = { "BOX TRAILER", "FLATBED", "TANKER" };
static const float trailer_mass[] = { 16000.0f, 20000.0f, 24000.0f };

#define MAX_NODES WG_MAX_PLACES

static float wrap(float a)
{
    while (a > WG_PI)
        a -= 2 * WG_PI;
    while (a < -WG_PI)
        a += 2 * WG_PI;
    return a;
}

static void graph(const world *w, const wg_graph **g, const wg_node **n, const wg_edge **e, const float **p)
{
    *g = world_graph(w, n, e, p);
}

/* Shortest road from node a to node b: the edges, in order; their count. */
static int shortest(const world *w, int a, int b, int *out_edges, float *metres)
{
    const wg_graph *g;
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    float dist[MAX_NODES];
    int prev_edge[MAX_NODES], done[MAX_NODES] = { 0 }, i, k, count = 0, at;
    graph(w, &g, &n, &e, &p);
    for (i = 0; i < (int)g->nnodes; i++) {
        dist[i] = 1e30f;
        prev_edge[i] = -1;
    }
    dist[a] = 0;
    for (k = 0; k < (int)g->nnodes; k++) {
        int u = -1;
        for (i = 0; i < (int)g->nnodes; i++)
            if (!done[i] && (u < 0 || dist[i] < dist[u]))
                u = i;
        if (u < 0 || dist[u] >= 1e29f)
            break;
        done[u] = 1;
        for (i = 0; i < (int)g->nedges; i++) {
            int v = (int)e[i].a == u ? (int)e[i].b : (int)e[i].b == u ? (int)e[i].a : -1;
            if (v >= 0 && dist[u] + e[i].length < dist[v]) {
                dist[v] = dist[u] + e[i].length;
                prev_edge[v] = i;
            }
        }
    }
    if (metres)
        *metres = dist[b];
    for (at = b; at != a && prev_edge[at] >= 0 && count < 32; count++) {
        out_edges[count] = prev_edge[at];
        at = (int)e[prev_edge[at]].a == at ? (int)e[prev_edge[at]].b : (int)e[prev_edge[at]].a;
    }
    for (i = 0; i < count / 2; i++) {                /* reversed: from a to b */
        int t = out_edges[i];
        out_edges[i] = out_edges[count - 1 - i];
        out_edges[count - 1 - i] = t;
    }
    return at == a ? count : -1;
}

static int nearest_node(const world *w, float x, float y)
{
    const wg_graph *g;
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    int i, best = 0;
    float bd = 1e30f;
    graph(w, &g, &n, &e, &p);
    for (i = 0; i < (int)g->nnodes; i++) {
        float dx = n[i].x - x, dy = n[i].y - y;
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

/* The road points from node `at` to the depot's gate, into route[]. */
static void route_from(jobs *j, const world *w, int at, int depot)
{
    const wg_graph *g;
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    int edges[32], ne, i, k;
    graph(w, &g, &n, &e, &p);
    ne = shortest(w, at, (int)w->depots[depot].node, edges, &j->route_left);
    j->nroute = j->route_next = 0;
    for (i = 0; i < ne; i++) {
        const wg_edge *ed = &e[edges[i]];
        int forward = (int)ed->a == at;
        for (k = i ? 1 : 0; k < (int)ed->npoints && j->nroute < ROUTE_MAX; k++) {   /* joins shared */
            int q = (int)ed->first_point + (forward ? k : (int)ed->npoints - 1 - k);
            j->route[j->nroute][0] = p[q * 2];
            j->route[j->nroute][1] = p[q * 2 + 1];
            j->nroute++;
        }
        at = forward ? (int)ed->b : (int)ed->a;
    }
}

void jobs_depot_uv(const world *w, int depot, float x, float y, float *u, float *v)
{
    const wg_depot *d = &w->depots[depot];
    float c = cosf(d->heading), s = sinf(d->heading), dx = x - d->x, dy = y - d->y;
    *u = dx * c + dy * s;
    *v = -dx * s + dy * c;
}

void jobs_bay(const jobs *j, const world *w, float *x, float *y, float *heading)
{
    const wg_depot *d = &w->depots[j->current.to];
    *x = d->bay[j->current.bay][0];
    *y = d->bay[j->current.bay][1];
    *heading = d->bay[j->current.bay][2];
}

void jobs_set_offer(jobs *j, const world *w, int i, int from, int to, int bay)
{
    job *o = &j->offers[i];
    int edges[32];
    o->from = from;
    o->to = to;
    o->bay = bay;
    o->type = j->trailers[from].type;
    o->mass = trailer_mass[o->type];
    shortest(w, (int)w->depots[from].node, (int)w->depots[to].node, edges, &o->distance);
    o->pay = 80.0f + o->distance / 1000.0f * 120.0f + (o->type == TRAILER_TANKER ? 40.0f : 0.0f);
    j->offers_at = from;
}

/* Three jobs for the trailer waiting at `depot`, to different depots. */
static void offer(jobs *j, const world *w, int depot)
{
    dgk_rng rng;
    int i, k, tries, to = 0;
    dgk_rng_seed(&rng, j->seed + (uint32_t)j->delivered * 7919u, (uint64_t)depot + 11);
    for (i = 0; i < JOB_OFFERS; i++) {
        for (tries = 0; tries < 16; tries++) {
            to = (int)dgk_rng_below(&rng, (uint32_t)w->ndepots);
            for (k = 0; k < i && j->offers[k].to != to; k++)
                continue;
            if (to != depot && (k == i || w->ndepots <= JOB_OFFERS))
                break;
        }
        jobs_set_offer(j, w, i, depot, to, (int)dgk_rng_below(&rng, WG_BAYS));
    }
}

void jobs_init(jobs *j, const world *w, uint32_t seed)
{
    int i;
    memset(j, 0, sizeof *j);
    j->seed = seed;
    for (i = 0; i < w->ndepots; i++) {
        j->trailers[i].x = w->depots[i].pickup[0];
        j->trailers[i].y = w->depots[i].pickup[1];
        j->trailers[i].heading = w->depots[i].pickup[2];
        j->trailers[i].type = i % TRAILER_TYPES;
        j->trailers[i].present = 1;
    }
    j->money = 250;
    j->at_depot = j->offers_at = -1;
    if (w->ndepots)
        offer(j, w, 0);
}

void jobs_take(jobs *j, const world *w, const rig *r, int n)
{
    if (j->state != JOB_NONE || j->offers_at < 0 || !j->trailers[j->offers_at].present)
        return;
    j->current = j->offers[n];
    j->state = JOB_TO_PICKUP;
    j->elapsed = 0;
    j->damage_start = r->damage;
    j->nroute = 0;
    DGK_UNUSED(w);
    dgk_log("FW-JOB take %s from depot %d to depot %d bay %d, %.0f m, pay %.0f", trailer_name[j->current.type],
            j->current.from, j->current.to, j->current.bay, j->current.distance, j->current.pay);
}

void jobs_cancel(jobs *j)
{
    if (j->state != JOB_TO_PICKUP)
        return;
    j->state = JOB_NONE;
    j->offers_at = -1;                                /* the board again, here */
    dgk_log("FW-JOB cancelled");
}

void jobs_props(const jobs *j, world *w, float x, float y)
{
    int i;
    w->nprops = 0;
    for (i = 0; i < w->ndepots; i++) {
        const wg_depot *d = &w->depots[i];
        if ((d->x - x) * (d->x - x) + (d->y - y) * (d->y - y) > 200.0f * 200.0f || w->nprops + 2 > WORLD_PROPS)
            continue;
        if (j->trailers[i].present)
            rig_parked_box(j->trailers[i].x, j->trailers[i].y, j->trailers[i].heading, &w->props[w->nprops++]);
        if (j->docked[i].present)
            rig_parked_box(j->docked[i].x, j->docked[i].y, j->docked[i].heading, &w->props[w->nprops++]);
    }
}

/* Fifth wheel within reach of the waiting trailer's kingpin, lined up, slow. */
static int can_couple(const rig *r, const parked *t)
{
    float hx, hy;
    rig_hitch(r, &hx, &hy);
    return !r->has_trailer && t->present && (hx - t->x) * (hx - t->x) + (hy - t->y) * (hy - t->y) < 0.6f * 0.6f &&
           fabsf(wrap(r->heading - t->heading)) < 12.0f * WG_PI / 180.0f && fabsf(r->v) < 1.5f;
}

int jobs_grade(float lat, float gap, float angle)
{
    float a = fabsf(angle) * 180.0f / WG_PI;
    lat = fabsf(lat);
    gap = fabsf(gap);
    if (lat < 0.25f && gap < 0.5f && a < 2.0f)
        return GRADE_PERFECT;
    if (lat < 0.5f && gap < 1.0f && a < 4.0f)
        return GRADE_GREAT;
    if (lat < 0.9f && gap < 1.6f && a < 7.0f)
        return GRADE_GOOD;
    if (lat < 1.5f && gap < 2.5f && a < 12.0f)
        return GRADE_OK;
    return GRADE_NONE;
}

/* The nearest route point to (x, y) from `from` to `to`; -1 if none is
 * within `within` metres. */
static int nearest_point(const jobs *j, float x, float y, int from, int to, float within)
{
    int i, best = -1;
    float bd = within * within;
    for (i = from; i < to; i++) {
        float dx = j->route[i][0] - x, dy = j->route[i][1] - y;
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

static void advance_route(jobs *j, int to)
{
    int i;
    for (i = j->route_next; i < to; i++)
        j->route_left -= sqrtf((j->route[i + 1][0] - j->route[i][0]) * (j->route[i + 1][0] - j->route[i][0]) +
                               (j->route[i + 1][1] - j->route[i][1]) * (j->route[i + 1][1] - j->route[i][1]));
    j->route_next = to;
}

/* Along the route: the nearest point in a window ahead. Far from all of
 * them, a new route from the nearest junction (at most every 3 s), picked
 * up where the rig is nearest to it. */
static void follow_route(jobs *j, const rig *r, const world *w)
{
    int best = nearest_point(j, r->x, r->y, j->route_next, DGK_MIN(j->route_next + 24, j->nroute), 60.0f);
    if (best >= 0) {
        if (best > j->route_next)
            advance_route(j, best);
        return;
    }
    if (j->state == JOB_HAULING && j->elapsed - j->rerouted_at > 3.0f) {
        j->rerouted_at = j->elapsed;
        route_from(j, w, nearest_node(w, r->x, r->y), j->current.to);
        best = nearest_point(j, r->x, r->y, 0, j->nroute, 1e6f);
        if (best > 0)
            advance_route(j, best);
        j->event = JOB_EVENT_REROUTED;
        dgk_log("FW-JOB rerouted: %.0f m", j->route_left);
    }
}

void jobs_tick(jobs *j, rig *r, const world *w, float dt)
{
    int i;
    j->event = JOB_EVENT_NONE;
    /* The depot the rig is on (the apron and the gate road), and its offers. */
    j->at_depot = -1;
    for (i = 0; i < w->ndepots; i++) {
        float u, v;
        jobs_depot_uv(w, i, r->x, r->y, &u, &v);
        if (fabsf(u) < 50.0f && v > -56.0f && v < 36.0f)
            j->at_depot = i;
        /* Out of sight, trailers change: a new one to take, the delivered one gone. */
        if ((w->depots[i].x - r->x) * (w->depots[i].x - r->x) + (w->depots[i].y - r->y) * (w->depots[i].y - r->y) >
            JOB_AWAY * JOB_AWAY) {
            if (!j->trailers[i].present) {
                j->trailers[i].present = 1;
                j->trailers[i].type = (j->trailers[i].type + 1) % TRAILER_TYPES;
            }
            j->docked[i].present = 0;
        }
    }
    if (j->state == JOB_NONE) {
        if (j->at_depot >= 0 && j->at_depot != j->offers_at)
            offer(j, w, j->at_depot);
        return;
    }
    j->elapsed += dt;
    if (j->state == JOB_TO_PICKUP) {
        parked *t = &j->trailers[j->current.from];
        if (can_couple(r, t)) {
            rig_couple(r, t->type, j->current.mass, t->heading);
            t->present = 0;
            j->state = JOB_HAULING;
            j->event = JOB_EVENT_COUPLED;
            route_from(j, w, (int)w->depots[j->current.from].node, j->current.to);
            j->rerouted_at = j->elapsed;
            dgk_log("FW-JOB coupled at %.1f s, %d road points, %.0f m", j->elapsed, j->nroute, j->route_left);
        }
        return;
    }
    follow_route(j, r, w);
    {
        float bx, by, bh, rx, ry, dx, dy;
        jobs_bay(j, w, &bx, &by, &bh);
        rig_trailer_rear(r, &rx, &ry);
        dx = rx - bx;
        dy = ry - by;
        /* In the bay's frame: gap along its heading (out of the bay), offset across. */
        j->gap = dx * cosf(bh) + dy * sinf(bh);
        j->lat = -dx * sinf(bh) + dy * cosf(bh);
        j->angle = wrap(r->trailer_heading - bh);
        if (j->state == JOB_HAULING && j->at_depot == j->current.to)
            j->state = JOB_DOCKING;
        else if (j->state == JOB_DOCKING && j->at_depot != j->current.to)
            j->state = JOB_HAULING;
    }
    if (j->state == JOB_DOCKING) {
        int g = jobs_grade(j->lat, j->gap, j->angle);
        j->still = fabsf(r->v) < 0.1f && g != GRADE_NONE ? j->still + dt : 0;
        if (j->still >= 1.5f) {
            parked *d = &j->docked[j->current.to];
            float target = j->current.distance / 12.0f * 1.4f + 90.0f;
            float timef = j->elapsed <= target ? 1.2f : DGK_MAX(0.5f, 1.0f - (j->elapsed - target) / 600.0f);
            float damage = r->damage - j->damage_start;
            static const int bonus[] = { 0, 0, 40, 80, 150 };
            j->grade = g;
            j->earned = (int)(j->current.pay * timef - damage * 4.0f) + bonus[g];
            if (j->earned < 10)
                j->earned = 10;
            j->money += j->earned;
            d->type = r->trailer_type;
            rig_uncouple(r, &d->x, &d->y, &d->heading);   /* the trailer stays at the dock */
            d->present = 1;
            j->delivered++;
            j->state = JOB_NONE;
            j->offers_at = -1;                            /* new offers here */
            j->event = JOB_EVENT_DELIVERED;
            dgk_log("FW-JOB done grade=%s lat=%.2f gap=%.2f angle=%.1f time=%.1f damage=%.1f earned=%d money=%d",
                    grade_name[g], j->lat, j->gap, j->angle * 180.0f / WG_PI, j->elapsed, damage, j->earned,
                    j->money);
        }
    }
}
