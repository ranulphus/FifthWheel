/* guides.h - a reversing camera's guide lines, on the ground behind the rig:
 * two lines where its back corners will go if it keeps reversing with the
 * wheels where they are (they bend as you steer), red near, yellow, then
 * green, with bars across at 1.5, 4 and 10 m. Drawn over the scene like a
 * real camera's overlay (no depth test). */
#ifndef FW_GUIDES_H
#define FW_GUIDES_H

#include "jobs.h"

void guides_draw(const rig *r, const world *w);

/* The route on the road ahead while hauling: gold chevrons pointing the way,
 * one at each road point (8 m apart) from 7 m to 48 m from the rig, sliding
 * forward with the ticks and fading in and out at the ends. Drawn over the
 * world (no depth test: a road can stand above the ground the rig rides),
 * before the shadows and the lorries. Nothing when the rig is off the route. */
void route_draw(const jobs *j, const rig *r, const world *w, uint32_t ticks);

/* Where the job wants the rig. Docking (or hauling within 150 m of the
 * bay): a pulsing gold outline round the bay, drawn on the ground like the
 * chevrons (before the shadows and the lorries; bay_draw); and a gold arrow
 * spinning and bobbing over the bay, or over the waiting trailer's nose
 * before coupling, drawn after the lorries (target_arrow_draw). */
void bay_draw(const jobs *j, const rig *r, const world *w, uint32_t ticks);
void target_arrow_draw(const jobs *j, const rig *r, const world *w, uint32_t ticks);

#endif
