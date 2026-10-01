/* jobs.h - delivery work: a trailer waits at every depot; a job takes it
 * from the depot you are at to a bay at another. The route runs over the
 * road graph (Dijkstra); at the far depot the trailer's back is reversed to
 * the dock and, stopped, graded; pay is for distance, less for lateness
 * and damage, more for a neat dock. Trailers come and go out of sight: a
 * new one waits where one was taken, and a delivered one is unloaded and
 * gone once you have driven away. */
#ifndef FW_JOBS_H
#define FW_JOBS_H

#include "vehicle.h"

#define JOB_OFFERS 3
#define ROUTE_MAX 6000
#define JOB_AWAY 250.0f             /* metres from a depot before its trailers change */

enum { JOB_NONE, JOB_TO_PICKUP, JOB_HAULING, JOB_DOCKING };
enum { GRADE_NONE, GRADE_OK, GRADE_GOOD, GRADE_GREAT, GRADE_PERFECT };
enum { JOB_EVENT_NONE, JOB_EVENT_COUPLED, JOB_EVENT_DELIVERED, JOB_EVENT_REROUTED };

typedef struct parked {
    float x, y, heading;            /* kingpin */
    int type, present;
} parked;

typedef struct job {
    int from, to, bay, type;        /* depots, the bay at `to` */
    float mass, distance, pay;      /* kg, metres (by road), the base pay */
} job;

typedef struct jobs {
    uint32_t seed;
    parked trailers[WG_MAX_PLACES]; /* waiting, one per depot */
    parked docked[WG_MAX_PLACES];   /* the last delivered, at its bay */
    job offers[JOB_OFFERS];
    job current;
    int state, event;               /* event: JOB_EVENT_* this tick */
    int at_depot, offers_at;        /* the depot the rig is on (-1: none); where the offers are from */
    int delivered, grade;           /* jobs done; the last one's grade */
    float elapsed, damage_start, still;
    float lat, gap, angle;          /* docking errors: metres, metres, radians */
    int money, earned;
    float route[ROUTE_MAX][2];      /* road points to the destination's gate */
    int nroute, route_next;
    float route_left;               /* metres of road left */
    float rerouted_at;              /* elapsed, at the last new route */
} jobs;

extern const char *const grade_name[];
extern const char *const trailer_name[];

void jobs_init(jobs *j, const world *w, uint32_t seed);
void jobs_take(jobs *j, const world *w, const rig *r, int offer);
void jobs_cancel(jobs *j);          /* a job not yet coupled */
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
/* The rig's position in a depot's frame: u along its front, v towards the warehouse. */
void jobs_depot_uv(const world *w, int depot, float x, float y, float *u, float *v);

#endif
