/* townroute.h - a way along a town's streets (wg_town's square grid) from
 * a point to a parcel stop: onto the nearest street, at most two corners,
 * round the roundabout (clockwise, as traffic goes) wherever the way meets
 * the town's middle, in the left lane, ending at the stop itself. Needs only
 * the town's record, so the host's tests run it too. */
#ifndef FW_TOWNROUTE_H
#define FW_TOWNROUTE_H

#include "wgen.h"

/* The town whose street square holds (x, y), `margin` metres beyond its
 * outer streets allowed; -1: none. */
int town_at(const wg_town *towns, int ntowns, float x, float y, float margin);

/* The way from (x0, y0), heading h0 (radians; the first street is taken in
 * the direction nearer it where there is a choice), to (x1, y1): points
 * about 6 m apart into out (at most max), their count (0: none). *metres:
 * its length. *backwards: 1 if it starts against h0 (a turn in the road
 * first). */
int town_path(const wg_town *t, float x0, float y0, float h0, float x1, float y1, float (*out)[2], int max,
              float *metres, int *backwards);

#endif
