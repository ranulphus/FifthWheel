/* traffic.h - toy cars on the roads: a dozen, keeping left (WG_LANE) and
 * going round the roundabouts clockwise, from town to town through the
 * junctions, never down a road that ends at a depot.
 *
 * A car is kinematic: pure pursuit along its lane, slowing for bends and
 * on a ring. It stops behind whatever is in its lane ahead (another car,
 * any part of the lorry), gives way at a roundabout to what is on it, and,
 * held up by the lorry for 6 s off a roundabout, turns round (15 s
 * anywhere: it goes, and comes back far out of sight). The lorry knocks a
 * car it hits aside: the car spins off, slides to a stop and finds its
 * lane again; the lorry takes a little damage, a third of a wall's.
 *
 * The same cars at every detail (they are the simulation, in FW-HASH);
 * the detail chooses only how they look. */
#ifndef FW_TRAFFIC_H
#define FW_TRAFFIC_H

#include "vehicle.h"

#define TRAFFIC_CARS 12                 /* how many, as a rule (-cars N: up to TRAFFIC_MAX) */
#define TRAFFIC_MAX 40
#define CAR_PATH_MAX 800
#define CAR_HL 2.0f                     /* half its length, half its width */
#define CAR_HW 0.9f

typedef struct car {
    float x, y, heading, v;             /* its middle; along its heading, m/s */
    float px, py, ph;                   /* the last tick's, to draw between */
    float path[CAR_PATH_MAX][2];        /* its lane to the next town's ring */
    int npath, next;                    /* the point it makes for */
    int ring_in, ring_out;              /* the points round the ring it starts on (-1: none) */
    int town, last_edge, first_edge;    /* where the path ends, the road into it; the path's first road */
    float end[2];                       /* the centre line's last point (on that town's ring) */
    int start_town, turned;             /* where it began; turned round since */
    float blocked;                      /* seconds held up by the lorry */
    float knocked, vx, vy, spin;        /* knocked aside: seconds left, its slide, its spin */
    int colour;
} car;

typedef struct traffic {
    car cars[TRAFFIC_MAX];
    int ncars;
    dgk_rng rng;
    uint8_t usable[64];                 /* the roads the cars take: those not to a dead end */
    int knocks;                         /* the lorry's knocks into cars */
    int turned, respawned, waits;       /* cars turned round, gone to come back; car-ticks giving way at a ring */
} traffic;

/* n cars (at most TRAFFIC_MAX), at least 120 m from (x, y) (where the rig
 * starts); none if the world has no roads. */
void traffic_init(traffic *t, const world *w, float x, float y, int n);
/* A tick: the cars move; the rig's knocks into them (its damage, hits). */
void traffic_tick(traffic *t, const world *w, rig *r, float dt);
/* Metres along a path (its points from `from`, from (x, y); closed: it
 * loops) to the first car in its lane, `max` if none within it. */
float traffic_gap(const traffic *t, const float *path, int npath, int from, int closed, float x, float y, float max);
uint32_t traffic_hash(const traffic *t, uint32_t h);
/* An FW-CARS line: how many move, their mean speed, the counts above. */
void traffic_log(const traffic *t);

/* The cars' meshes, `quality` round (detail.h's models); again when the
 * detail changes. */
void traffic_build(int quality);
/* Their shadows (between lorry_shadows_begin and _end), then the cars:
 * those within `reach` of (cx, cy), between the last tick and this. */
void traffic_shadows(const traffic *t, const world *w, float alpha, float cx, float cy, float reach);
void traffic_draw(const traffic *t, const world *w, float alpha, float cx, float cy, float reach);

#endif
