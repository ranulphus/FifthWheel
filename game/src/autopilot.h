/* autopilot.h - drives the rig round the world's path (pure pursuit on the
 * tractor, a speed controller on the pedals). It makes the same inputs a
 * player would, so replays and hashes cover it; tests and the timedemo use it. */
#ifndef FW_AUTOPILOT_H
#define FW_AUTOPILOT_H

#include "vehicle.h"

typedef struct autopilot {
    int next;                   /* path point being approached */
    int laps, half;             /* completed laps; passed the far half this lap */
    float speed;                /* target, m/s */
} autopilot;

void autopilot_reset(autopilot *a, const rig *r, const world *w);
void autopilot_drive(autopilot *a, const rig *r, const world *w, rig_input *in);

#endif
