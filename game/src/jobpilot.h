/* jobpilot.h - the autopilot for a whole job (tests, the timedemo): back the
 * lone tractor under the waiting trailer, drive out of the depot and along
 * the road to the other depot, loop round on its apron to line up with the
 * bay, and reverse the trailer into it. It makes the same pedal and
 * steering inputs a player would.
 *
 * Forward: pure pursuit on the tractor's rear axle, along the route with
 * its junction corners rounded into arcs a rig can take. Reversing alone: pure
 * pursuit on the same axle, steering mirrored. Reversing the trailer: pure
 * pursuit on the trailer's axle gives the trailer's wanted curvature; the
 * kingpin kinematics turn that into a wanted articulation; the tractor
 * steers for the yaw rate that holds it.
 *
 * It also does the always-there work (the career's tests): a parcel round
 * (along the round's route to each stop, out of a depot's gate first if
 * the rig is on one), and shunts (from the line into a bay round a loop on
 * the apron, from a bay back onto the line); and it lines a lone tractor up
 * ahead of a trailer it is to take, and drives a lone tractor to another
 * depot (jobpilot_travel). */
#ifndef FW_JOBPILOT_H
#define FW_JOBPILOT_H

#include "jobs.h"
#include "traffic.h"

#define JP_PATH_MAX (ROUTE_MAX + 64)

enum { JP_COUPLE, JP_DRIVE, JP_REVERSE, JP_DONE, JP_LINEUP, JP_ROUND, JP_TRAVEL };

typedef struct jobpilot {
    int phase, delivered;               /* delivered: jobs done when this one began */
    float path[JP_PATH_MAX][2];         /* forward, from the pickup to the bay's staging point */
    float along[JP_PATH_MAX];           /* metres from the path's start */
    int npath, next;
    int shunts;                         /* shunts done when this one began */
    int stop, nroute;                   /* a round's: the stop and route length the path was made for */
    float route0[2];
    float top;                          /* the path's top speed, m/s */
    float streets_from, streets_to;     /* metres along it: town streets between (8 m/s at most) */
} jobpilot;

void jobpilot_reset(jobpilot *p, const jobs *j);
/* t: the cars, to stop behind one in its lane on the road (NULL: none). */
void jobpilot_drive(jobpilot *p, const jobs *j, const rig *r, const world *w, const traffic *t, rig_input *in);
/* Where a lone tractor waits to take a depot's trailer: straight ahead of
 * it, lined up (its rear axle 9 m ahead of the kingpin). */
void jobpilot_start(const jobs *j, int depot, float *x, float *y, float *heading);
/* A lone tractor, no job on: along j's route (jobs_travel's, to `depot`'s
 * gate) and in, lined up ahead of the trailer on its line. */
void jobpilot_travel(jobpilot *p, const jobs *j, const rig *r, const world *w, int depot);
/* Nothing left to drive (a round's stop, a journey's end): stopped. */
int  jobpilot_idle(const jobpilot *p, const rig *r);

#endif
