/* jobs.h - delivery work: a trailer waits at every depot; a job takes it
 * from the depot you are at to a bay at another. The route runs over the
 * road graph (Dijkstra); at the far depot the trailer's back is reversed to
 * the dock and, stopped, graded; pay is for distance, less for lateness
 * and damage, more for a neat dock. Trailers come and go out of sight: a
 * new one waits where one was taken, and a delivered one is unloaded and
 * gone once you have driven away.
 *
 * Work that is always there, for slower money (no licence, no trailer of
 * the right kind needed): a parcel round (R, a lone tractor: parcels in the
 * cab, one stop after another at house and shop doors in the nearest town,
 * stopping in a ring at each, paid a little more for each drop in a row on
 * time), and shunting at a depot (board key 4: its yard trailer from the
 * line into a bay, or from a bay back onto the line, graded like a dock). */
#ifndef FW_JOBS_H
#define FW_JOBS_H

#include "vehicle.h"

#define JOB_OFFERS 3
#define ROUTE_MAX 6000
#define JOB_AWAY 250.0f             /* metres from a depot before its trailers change */

enum { JOB_NONE, JOB_TO_PICKUP, JOB_HAULING, JOB_DOCKING };
enum { GRADE_NONE, GRADE_OK, GRADE_GOOD, GRADE_GREAT, GRADE_PERFECT };
enum { JOB_EVENT_NONE, JOB_EVENT_COUPLED, JOB_EVENT_DELIVERED, JOB_EVENT_REROUTED, JOB_EVENT_DROPPED,
       JOB_EVENT_ROUND_OVER, JOB_EVENT_SHUNTED };
enum { JOB_KIND_DEPOT, JOB_KIND_PARCEL, JOB_KIND_SHUNT };
enum { SHUNT_TO_BAY, SHUNT_TO_LINE };   /* a shunt: the line's trailer into a bay, or a bay's back onto the line */
#define JOB_SHUNT_KEY JOB_OFFERS        /* the board's fourth line (key 4) */

typedef struct parked {
    float x, y, heading;            /* kingpin */
    int type, present;
    int bay;                        /* docked: its bay */
} parked;

typedef struct job {
    int from, to, bay, type;        /* depots, the bay at `to` */
    float mass, distance, pay;      /* kg, metres (by road), the base pay */
    int kind, way;                  /* JOB_KIND_*; a shunt's SHUNT_* */
} job;

typedef struct jobs {
    uint32_t seed;
    parked trailers[WG_MAX_PLACES]; /* waiting, one per depot */
    parked docked[WG_MAX_PLACES];   /* the last delivered, at its bay */
    job offers[JOB_OFFERS];
    job shunt;                      /* the yard's work at offers_at (shunt.kind: JOB_KIND_SHUNT; -1: none) */
    job current;
    int state, event;               /* event: JOB_EVENT_* this tick */
    int at_depot, offers_at;        /* the depot the rig is on (-1: none); where the offers are from */
    int delivered, grade;           /* jobs done; the last one's grade */
    float elapsed, damage_start, still;
    float lat, gap, angle;          /* docking errors: metres, metres, radians */
    int money, earned;
    uint32_t licences;              /* a bit per trailer type the player may haul (all, unless a career says) */
    float route[ROUTE_MAX][2];      /* road points to the destination's gate */
    int nroute, route_next;
    int street0, street1;           /* route[street0 .. street1): along a town's streets (its corners rounded) */
    float route_left;               /* metres of road left */
    float rerouted_at;              /* elapsed, at the last new route */
    /* A parcel round (current.kind JOB_KIND_PARCEL, state JOB_HAULING throughout). */
    int town, stop;                 /* its town; the stop now (index into the world's stops) */
    int drops, rounds, streak;      /* drops ever, rounds begun, drops in a row on time */
    int round_drops, round_earned;
    float leg, limit;               /* this drop's metres from the last, and its seconds on time */
    int shunts;                     /* shunts done */
    int round_town;                 /* the town the next round is in (-1: the nearest; tests choose) */
    int pending;                    /* an event from a key (a round ended), given by the next tick */
} jobs;

extern const char *const grade_name[];
extern const char *const trailer_name[];

void jobs_init(jobs *j, const world *w, uint32_t seed);
void jobs_take(jobs *j, const world *w, const rig *r, int offer);
void jobs_cancel(jobs *j);          /* a job not yet coupled */
/* R: begin a parcel round in the nearest town (a lone tractor, no job on),
 * or end the one going on. */
void jobs_round(jobs *j, const world *w, const rig *r);
/* No job on: the route (into route[]) to depot's gate, from the depot the
 * rig is on, or out of the town it is in along its streets, or the nearest
 * node (the career's tests drive a lone tractor along it). */
void jobs_travel(jobs *j, const world *w, const rig *r, int depot);
/* Board key 4: take the shunt on offer at this depot. */
void jobs_take_shunt(jobs *j, const rig *r);
/* What a parcel drop pays: for its metres from the last, a shop's or a
 * house's, the drops before it in a row on time, damage on the way. */
int jobs_drop_pay(float metres, int shop, int streak, float damage_on_leg, float damage);
/* Tests: make offer i a given job (the trailer waiting at `from`). */
void jobs_set_offer(jobs *j, const world *w, int i, int from, int to, int bay);
/* Before the rig's step: the trailers standing nearby, as the world's props. */
void jobs_props(const jobs *j, world *w, float x, float y);
/* After it: coupling, route progress, docking and grading, trailers
 * coming and going, the offers at the depot the rig is on. */
void jobs_tick(jobs *j, rig *r, const world *w, float dt);
/* The current job's bay: the trailer's back when docked, pointing out. */
void jobs_bay(const jobs *j, const world *w, float *x, float *y, float *heading);
/* The grade a dock with these errors earns (GRADE_NONE: not docked). */
int jobs_grade(float lat, float gap, float angle);
/* What heavy damage leaves of any pay: all of it to 50 points, half at 100. */
float jobs_damage_factor(float damage);
/* The rig's position in a depot's frame: u along its front, v towards the warehouse. */
void jobs_depot_uv(const world *w, int depot, float x, float y, float *u, float *v);

#endif
