/* jobs.c - delivery work (see jobs.h). */
#include "jobs.h"
#include "townroute.h"
#include "dgk/log.h"
#include <math.h>
#include <string.h>

const char *const grade_name[] = { "", "OK", "GOOD", "GREAT", "PERFECT" };
const char *const trailer_name[] = { "BOX TRAILER", "FLATBED", "TANKER" };
static const float trailer_mass[] = { 16000.0f, 20000.0f, 24000.0f };

#define MAX_NODES WG_MAX_NODES
#define SHUNT_PAY 30.0f                 /* a shunt's base pay, before its grade's bonus */
#define DROP_RADIUS 4.0f                /* metres from a parcel stop: the cab in its ring */
#define DROP_STILL 0.8f                 /* seconds stopped there */
#define MAX_ROUTE_EDGES 64              /* a route's roads at most (junctions cut a road in two) */

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

/* A road's way at a node (unit, about 24 m of it): leaving the node, or (in) arriving at it. */
#define JOBS_MAX_EDGES 64
static void way_at(const wg_edge *e, const float *p, int at_a, int in, float *dx, float *dy)
{
    int k = DGK_MIN(3, (int)e->npoints - 1), f = (int)e->first_point, l = (int)(e->first_point + e->npoints - 1);
    int from = at_a ? f : l, to = at_a ? f + k : l - k;
    float x = p[to * 2] - p[from * 2], y = p[to * 2 + 1] - p[from * 2 + 1], len = sqrtf(x * x + y * y) + 1e-6f;
    *dx = (in ? -x : x) / len;
    *dy = (in ? -y : y) / len;
}

/* Shortest road from node a to node b: the edges, in order (a road may come twice); their count (-1: none).
 * It never turns back at a junction: a fork's branches leave it at a narrow angle, and from one to the other
 * is a hairpin no lorry takes; it goes on and round a roundabout instead (back the way it came only there).
 * Dijkstra over the roads each way (state 2e: along e from its a to its b; 2e + 1: back), a depot a dead end. */
static int shortest(const world *w, int a, int b, int *out_edges, float *metres)
{
    const wg_graph *g;
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    float dist[2 * JOBS_MAX_EDGES], best = 1e30f;
    int prev[2 * JOBS_MAX_EDGES], done[2 * JOBS_MAX_EDGES] = { 0 }, ns, i, k, s, end = -1, count = 0;
    graph(w, &g, &n, &e, &p);
    if (metres)
        *metres = 0;
    if (a == b)
        return 0;
    if ((int)g->nedges > JOBS_MAX_EDGES) {
        dgk_log("FW-ERROR jobs: %u roads, room for %d", (unsigned)g->nedges, JOBS_MAX_EDGES);
        return -1;
    }
    ns = 2 * (int)g->nedges;
    for (s = 0; s < ns; s++) {
        int leaves = s & 1 ? (int)e[s / 2].b : (int)e[s / 2].a;
        dist[s] = leaves == a ? e[s / 2].length : 1e30f;
        prev[s] = -1;
    }
    for (k = 0; k < ns; k++) {
        int u = -1, v, ui;
        float ix, iy;
        for (s = 0; s < ns; s++)
            if (!done[s] && (u < 0 || dist[s] < dist[u]))
                u = s;
        if (u < 0 || dist[u] >= 1e29f)
            break;
        done[u] = 1;
        ui = u / 2;
        v = u & 1 ? (int)e[ui].a : (int)e[ui].b;           /* where it arrives */
        if (v == b && dist[u] < best) {
            best = dist[u];
            end = u;
        }
        if (n[v].kind == WG_DEPOT)
            continue;
        way_at(&e[ui], p, (int)e[ui].a == v, 1, &ix, &iy);
        for (i = 0; i < (int)g->nedges; i++) {
            float ox, oy;
            int t;
            if ((int)e[i].a != v && (int)e[i].b != v)
                continue;
            t = 2 * i + ((int)e[i].a == v ? 0 : 1);
            if (i == ui && n[v].kind != WG_TOWN)
                continue;
            if (n[v].kind == WG_JUNCTION) {
                way_at(&e[i], p, (int)e[i].a == v, 0, &ox, &oy);
                if (ix * ox + iy * oy < -0.5f)              /* turning back more than 120 degrees */
                    continue;
            }
            if (dist[u] + e[i].length < dist[t]) {
                dist[t] = dist[u] + e[i].length;
                prev[t] = u;
            }
        }
    }
    if (end < 0)
        return -1;
    if (metres)
        *metres = best;
    for (s = end; s >= 0 && count < MAX_ROUTE_EDGES; s = prev[s])
        out_edges[count++] = s / 2;
    for (i = 0; i < count / 2; i++) {                /* reversed: from a to b */
        int t = out_edges[i];
        out_edges[i] = out_edges[count - 1 - i];
        out_edges[count - 1 - i] = t;
    }
    return count;
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

/* The road points from node `at` to node `to` (a depot's gate, a town's ring), into route[]. */
static void route_nodes(jobs *j, const world *w, int at, int to)
{
    const wg_graph *g;
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    int edges[MAX_ROUTE_EDGES], ne, i, k;
    graph(w, &g, &n, &e, &p);
    ne = shortest(w, at, to, edges, &j->route_left);
    j->nroute = j->route_next = 0;
    j->street0 = j->street1 = 0;
    for (i = 0; i < ne; i++) {
        const wg_edge *ed = &e[edges[i]];
        int forward = (int)ed->a == at, ring = i > 0 && n[at].kind == WG_TOWN && j->nroute > 0;
        if (ring) {
            /* Through a town: round its roundabout, clockwise, from the ring's point where the last road
             * stopped to this one's. */
            int q = forward ? (int)ed->first_point : (int)(ed->first_point + ed->npoints - 1);
            j->nroute += wg_ring_arc(n[at].x, n[at].y, j->route[j->nroute - 1][0], j->route[j->nroute - 1][1],
                                     p[q * 2], p[q * 2 + 1], &j->route[j->nroute], ROUTE_MAX - j->nroute);
        }
        for (k = i && !ring ? 1 : 0; k < (int)ed->npoints && j->nroute < ROUTE_MAX; k++) {   /* joins shared */
            int q = (int)ed->first_point + (forward ? k : (int)ed->npoints - 1 - k);
            j->route[j->nroute][0] = p[q * 2];
            j->route[j->nroute][1] = p[q * 2 + 1];
            j->nroute++;
        }
        at = forward ? (int)ed->b : (int)ed->a;
    }
    {
        /* In the left lane. */
        static float centre[ROUTE_MAX][2];
        memcpy(centre, j->route, sizeof(float) * 2 * (size_t)j->nroute);
        wg_offset((const float (*)[2])centre, j->nroute, WG_LANE, 0, j->route);
    }
}

static void route_from(jobs *j, const world *w, int at, int depot)
{
    route_nodes(j, w, at, (int)w->depots[depot].node);
}

float jobs_damage_factor(float damage)
{
    return damage > 50.0f ? 1.0f - DGK_MIN(damage - 50.0f, 50.0f) / 100.0f : 1.0f;
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
    if (j->current.kind == JOB_KIND_PARCEL) {
        const wg_stop *s = &w->stops[j->stop];
        *x = s->x;
        *y = s->y;
        *heading = s->heading;
        return;
    }
    if (j->current.kind == JOB_KIND_SHUNT && j->current.way == SHUNT_TO_LINE) {
        /* The line's place: where a trailer waiting there has its back, pointing along the line. */
        *heading = d->pickup[2];
        *x = d->pickup[0] - RIG_TRAILER_REAR * cosf(*heading);
        *y = d->pickup[1] - RIG_TRAILER_REAR * sinf(*heading);
        return;
    }
    *x = d->bay[j->current.bay][0];
    *y = d->bay[j->current.bay][1];
    *heading = d->bay[j->current.bay][2];
}

void jobs_set_offer(jobs *j, const world *w, int i, int from, int to, int bay)
{
    job *o = &j->offers[i];
    int edges[MAX_ROUTE_EDGES];
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
    /* The yard's shunt: the line's trailer into a free bay, or (the line empty) the docked one back onto it. */
    memset(&j->shunt, 0, sizeof j->shunt);
    j->shunt.kind = -1;
    if (j->trailers[depot].present) {
        int bay = (int)dgk_rng_below(&rng, WG_BAYS);
        if (j->docked[depot].present && bay == j->docked[depot].bay)
            bay = (bay + 1) % WG_BAYS;
        j->shunt.way = SHUNT_TO_BAY;
        j->shunt.bay = bay;
        j->shunt.type = j->trailers[depot].type;
    } else if (j->docked[depot].present) {
        j->shunt.way = SHUNT_TO_LINE;
        j->shunt.bay = j->docked[depot].bay;
        j->shunt.type = j->docked[depot].type;
    } else
        return;
    j->shunt.kind = JOB_KIND_SHUNT;
    j->shunt.from = j->shunt.to = depot;
    j->shunt.mass = trailer_mass[j->shunt.type];
    j->shunt.pay = SHUNT_PAY;
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
    j->round_town = -1;
    j->licences = (1u << TRAILER_TYPES) - 1;
    j->at_depot = j->offers_at = -1;
    if (w->ndepots)
        offer(j, w, 0);
}

void jobs_take(jobs *j, const world *w, const rig *r, int n)
{
    if (j->state != JOB_NONE || j->offers_at < 0 || !j->trailers[j->offers_at].present)
        return;
    if (!(j->licences >> j->offers[n].type & 1u)) {
        dgk_log("FW-JOB locked: no licence for the %s", trailer_name[j->offers[n].type]);
        return;
    }
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

/* ---- parcel rounds ----------------------------------------------------------- */


/* The way to the stop: along the town's streets, after the roads to the town if the rig is not in it. */
static void parcel_route(jobs *j, const world *w, const rig *r)
{
    const wg_town *t = &w->towns[j->town];
    const wg_stop *s = &w->stops[j->stop];
    float fx = r->x, fy = r->y, fh = r->heading, metres = 0;
    int back;
    j->nroute = j->route_next = 0;
    j->street0 = j->street1 = 0;
    j->route_left = 0;
    if (town_at(w->towns, w->ntowns, r->x, r->y, 40.0f) != j->town) {
        route_nodes(j, w, j->at_depot >= 0 ? (int)w->depots[j->at_depot].node : nearest_node(w, r->x, r->y),
                    (int)t->node);
        if (j->nroute >= 2) {                        /* the streets from the roundabout, where the road reaches it */
            fx = j->route[j->nroute - 1][0];
            fy = j->route[j->nroute - 1][1];
            fh = atan2f(fy - j->route[j->nroute - 2][1], fx - j->route[j->nroute - 2][0]);
        }
    }
    j->street0 = j->nroute;
    j->nroute += town_path(t, fx, fy, fh, s->x, s->y, &j->route[j->nroute], ROUTE_MAX - j->nroute, &metres, &back);
    j->street1 = j->nroute;
    j->route_left += metres;
}

/* The next stop: another in the town, 70 to 260 m off as the crow flies, the way to it starting the way the
 * rig faces (if one of 24 tries is); outside the town, any. */
static int pick_stop(jobs *j, const world *w, const rig *r)
{
    static float way[ROUTE_MAX][2];
    const wg_town *t = &w->towns[j->town];
    int in = town_at(w->towns, w->ntowns, r->x, r->y, 40.0f) == j->town, tries, s = -1;
    dgk_rng rng;
    dgk_rng_seed(&rng, j->seed + (uint32_t)j->drops * 7919u + (uint32_t)j->rounds * 104729u, (uint64_t)j->town + 5);
    if (t->nstops == 0)
        return -1;
    for (tries = 0; tries < 48; tries++) {
        float dx, dy, d2, metres;
        int back;
        s = (int)t->first_stop + (int)dgk_rng_below(&rng, t->nstops);
        if (s == j->stop || !in)
            continue;
        dx = w->stops[s].x - r->x;
        dy = w->stops[s].y - r->y;
        d2 = dx * dx + dy * dy;
        if (d2 < 70.0f * 70.0f || d2 > 260.0f * 260.0f)
            continue;
        town_path(t, r->x, r->y, r->heading, w->stops[s].x, w->stops[s].y, way, ROUTE_MAX, &metres, &back);
        if (!back || tries >= 24)
            return s;
    }
    return s;
}

int jobs_drop_pay(float metres, int shop, int streak, float damage_on_leg, float damage)
{
    float pay = 10.0f + 0.05f * metres + (shop ? 4.0f : 0.0f) + 3.0f * (float)DGK_MIN(streak, 5) - 2.0f * damage_on_leg;
    pay *= jobs_damage_factor(damage);
    return pay < 5.0f ? 5 : (int)pay;
}

/* On to the next stop (a new leg: its way, its metres, its time). */
static void next_stop(jobs *j, const world *w, const rig *r)
{
    j->stop = pick_stop(j, w, r);
    parcel_route(j, w, r);
    j->leg = j->route_left;
    j->limit = j->leg / 6.0f + 20.0f;
    j->elapsed = 0;
    j->rerouted_at = 0;
    j->still = 0;
    j->damage_start = r->damage;
}

void jobs_round(jobs *j, const world *w, const rig *r)
{
    int i, best = -1;
    float bd = 1e30f;
    if (j->state == JOB_HAULING && j->current.kind == JOB_KIND_PARCEL) {
        j->state = JOB_NONE;
        j->offers_at = -1;
        j->pending = JOB_EVENT_ROUND_OVER;
        dgk_log("FW-JOB round over: %d drops, earned %d, money %d", j->round_drops, j->round_earned, j->money);
        return;
    }
    if (j->state != JOB_NONE || r->has_trailer || w->ntowns == 0) {
        dgk_log("FW-JOB round: not now (%s)", r->has_trailer ? "a trailer on" : j->state != JOB_NONE ? "a job on"
                                                                                                    : "no towns");
        return;
    }
    for (i = 0; i < w->ntowns; i++) {
        float dx = w->towns[i].x - r->x, dy = w->towns[i].y - r->y;
        if (w->towns[i].nstops && dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    if (j->round_town >= 0 && j->round_town < w->ntowns && w->towns[j->round_town].nstops)
        best = j->round_town;
    if (best < 0)
        return;
    memset(&j->current, 0, sizeof j->current);
    j->current.kind = JOB_KIND_PARCEL;
    j->current.from = j->current.to = -1;
    j->town = best;
    j->stop = -1;
    j->rounds++;
    j->round_drops = j->round_earned = j->streak = 0;
    j->state = JOB_HAULING;
    next_stop(j, w, r);
    dgk_log("FW-JOB round in town %d: first stop %d, %.0f m", j->town, j->stop, j->leg);
}

/* A parcel round's tick: along the way, and the drop when the cab stops in the stop's ring. */
static void parcel_tick(jobs *j, const rig *r, const world *w, float dt)
{
    const wg_stop *s = &w->stops[j->stop];
    float cx = r->x + 2.0f * cosf(r->heading) - s->x, cy = r->y + 2.0f * sinf(r->heading) - s->y;
    int best = nearest_point(j, r->x, r->y, j->route_next, DGK_MIN(j->route_next + 24, j->nroute), 60.0f);
    if (best > j->route_next)
        advance_route(j, best);
    else if (best < 0 && j->elapsed - j->rerouted_at > 3.0f) {
        j->rerouted_at = j->elapsed;
        parcel_route(j, w, r);
        j->event = JOB_EVENT_REROUTED;
        dgk_log("FW-JOB parcel rerouted: %.0f m", j->route_left);
    }
    j->still = cx * cx + cy * cy < DROP_RADIUS * DROP_RADIUS && fabsf(r->v) < 0.4f ? j->still + dt : 0;
    if (j->still < DROP_STILL)
        return;
    {
        int on_time = j->elapsed <= j->limit;
        j->streak = on_time ? j->streak + 1 : 0;
        j->earned = jobs_drop_pay(j->leg, s->kind == WG_STOP_SHOP, j->streak, r->damage - j->damage_start, r->damage);
        j->money += j->earned;
        j->round_earned += j->earned;
        j->drops++;
        j->round_drops++;
        j->event = JOB_EVENT_DROPPED;
        dgk_log("FW-JOB drop %d at stop %d (%s): %.0f m in %.1f s (%s, streak %d), earned %d, money %d", j->round_drops,
                j->stop, s->kind == WG_STOP_SHOP ? "shop" : "house", j->leg, j->elapsed, on_time ? "on time" : "late",
                j->streak, j->earned, j->money);
        next_stop(j, w, r);
    }
}

void jobs_travel(jobs *j, const world *w, const rig *r, int depot)
{
    static float pre[ROUTE_MAX][2];
    int town = j->at_depot >= 0 ? -1 : town_at(w->towns, w->ntowns, r->x, r->y, 40.0f), n = 0, back;
    float metres = 0;
    route_nodes(j, w, j->at_depot >= 0 ? (int)w->depots[j->at_depot].node : town >= 0 ? (int)w->towns[town].node
                                                                                      : nearest_node(w, r->x, r->y),
                (int)w->depots[depot].node);
    if (town >= 0 && j->nroute > 0) {                /* the town's streets to its roundabout, where the road leaves */
        n = town_path(&w->towns[town], r->x, r->y, r->heading, j->route[0][0], j->route[0][1], pre, ROUTE_MAX,
                      &metres, &back);
        n = DGK_MIN(n, ROUTE_MAX - j->nroute);
        memmove(&j->route[n], j->route, sizeof(float) * 2 * (size_t)j->nroute);
        memcpy(j->route, pre, sizeof(float) * 2 * (size_t)n);
        j->nroute += n;
        j->route_left += metres;
        j->street0 = 0;
        j->street1 = n;
    }
    j->route_next = 0;
    dgk_log("FW-JOB travel to depot %d: %d road points, %.0f m", depot, j->nroute, j->route_left);
}

/* ---- shunts ---------------------------------------------------------------- */

void jobs_take_shunt(jobs *j, const rig *r)
{
    if (j->state != JOB_NONE || j->offers_at < 0 || j->offers_at != j->at_depot || j->shunt.kind != JOB_KIND_SHUNT ||
        r->has_trailer)
        return;
    j->current = j->shunt;
    j->state = JOB_TO_PICKUP;
    j->elapsed = 0;
    j->damage_start = r->damage;
    j->nroute = j->route_next = 0;
    j->route_left = 0;
    dgk_log("FW-JOB shunt the %s at depot %d %s bay %d", trailer_name[j->current.type], j->current.from,
            j->current.way == SHUNT_TO_BAY ? "from the line to" : "back to the line from", j->current.bay);
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
    if (j->state == JOB_HAULING && j->current.kind == JOB_KIND_DEPOT && j->elapsed - j->rerouted_at > 3.0f) {
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
    j->event = j->pending;
    j->pending = JOB_EVENT_NONE;
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
    if (j->current.kind == JOB_KIND_PARCEL) {
        parcel_tick(j, r, w, dt);
        return;
    }
    if (j->state == JOB_TO_PICKUP) {
        int shunt = j->current.kind == JOB_KIND_SHUNT;
        parked *t = shunt && j->current.way == SHUNT_TO_LINE ? &j->docked[j->current.from] : &j->trailers[j->current.from];
        if (can_couple(r, t)) {
            rig_couple(r, t->type, j->current.mass, t->heading);
            t->present = 0;
            j->state = JOB_HAULING;
            j->event = JOB_EVENT_COUPLED;
            if (!shunt)
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
        if (j->still >= 1.5f && j->current.kind == JOB_KIND_SHUNT) {
            /* A shunt: its pay and its grade's bonus; the trailer left in the bay, or back on the line. */
            static const int bonus[] = { 0, 0, 10, 20, 30 };
            float damage = r->damage - j->damage_start;
            parked *d = j->current.way == SHUNT_TO_BAY ? &j->docked[j->current.to] : &j->trailers[j->current.to];
            j->grade = g;
            j->earned = (int)((j->current.pay + (float)bonus[g] - damage * 2.0f) * jobs_damage_factor(r->damage));
            if (j->earned < 10)
                j->earned = 10;
            j->money += j->earned;
            d->type = r->trailer_type;
            d->bay = j->current.bay;
            rig_uncouple(r, &d->x, &d->y, &d->heading);
            d->present = 1;
            j->shunts++;
            j->state = JOB_NONE;
            j->offers_at = -1;                            /* the next shunt, the other way */
            j->event = JOB_EVENT_SHUNTED;
            dgk_log("FW-JOB shunted grade=%s lat=%.2f gap=%.2f angle=%.1f time=%.1f damage=%.1f earned=%d money=%d",
                    grade_name[g], j->lat, j->gap, j->angle * 180.0f / WG_PI, j->elapsed, damage, j->earned,
                    j->money);
        } else if (j->still >= 1.5f) {
            parked *d = &j->docked[j->current.to];
            float target = j->current.distance / 12.0f * 1.4f + 90.0f;
            float timef = j->elapsed <= target ? 1.2f : DGK_MAX(0.5f, 1.0f - (j->elapsed - target) / 600.0f);
            float damage = r->damage - j->damage_start;
            static const int bonus[] = { 0, 0, 40, 80, 150 };
            j->grade = g;
            j->earned = (int)(((float)j->current.pay * timef - damage * 4.0f + (float)bonus[g]) *
                              jobs_damage_factor(r->damage));
            if (j->earned < 10)
                j->earned = 10;
            j->money += j->earned;
            d->type = r->trailer_type;
            d->bay = j->current.bay;
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
