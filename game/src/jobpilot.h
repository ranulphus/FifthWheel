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
 * steers for the yaw rate that holds it. */
#ifndef FW_JOBPILOT_H
#define FW_JOBPILOT_H

#include "jobs.h"

#define JP_PATH_MAX (ROUTE_MAX + 64)

enum { JP_COUPLE, JP_DRIVE, JP_REVERSE, JP_DONE };

typedef struct jobpilot {
    int phase, delivered;               /* delivered: jobs done when this one began */
    float path[JP_PATH_MAX][2];         /* forward, from the pickup to the bay's staging point */
    float along[JP_PATH_MAX];           /* metres from the path's start */
    int npath, next;
} jobpilot;

void jobpilot_reset(jobpilot *p, const jobs *j);
void jobpilot_drive(jobpilot *p, const jobs *j, const rig *r, const world *w, rig_input *in);
/* Where a lone tractor waits to take a depot's trailer: straight ahead of
 * it, lined up (its rear axle 9 m ahead of the kingpin). */
void jobpilot_start(const jobs *j, int depot, float *x, float *y, float *heading);

#endif
