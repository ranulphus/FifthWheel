/* guides.h - a reversing camera's guide lines, on the ground behind the rig:
 * two lines where its back corners will go if it keeps reversing with the
 * wheels where they are (they bend as you steer), red near, yellow, then
 * green, with bars across at 1.5, 4 and 10 m. Drawn over the scene like a
 * real camera's overlay (no depth test). */
#ifndef FW_GUIDES_H
#define FW_GUIDES_H

#include "vehicle.h"

void guides_draw(const rig *r, const world *w);

#endif
