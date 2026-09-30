/* sound.h - the lorry's sounds, synthesised at start-up: a six-cylinder
 * diesel loop pitched by rpm and loudened by the throttle, air-brake hiss
 * when the brakes let go, a reversing beeper, a gear-change puff. */
#ifndef FW_SOUND_H
#define FW_SOUND_H

#include "vehicle.h"

void sound_init(void);
void sound_tick(const rig *r);       /* after each simulation tick */
void sound_stop(void);

#endif
