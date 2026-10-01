/* sound.h - the lorry's sounds, synthesised at start-up: a six-cylinder
 * diesel as two loops (a deep, knocking low one and a raspy high one with a
 * turbo whistle) crossfaded and pitched by rpm, loudened by the throttle;
 * air-brake hiss when the brakes let go, a reversing beeper, a gear-change
 * puff; a horn while it is held (two-tone, a big air horn's chord, a jingle
 * or a duck); a clunk when the fifth wheel locks, a thud on a collision, a
 * screech on a jackknife, a chime for a delivery (higher for a better
 * grade). */
#ifndef FW_SOUND_H
#define FW_SOUND_H

#include "vehicle.h"

void sound_init(void);
void sound_tick(const rig *r, int horn);   /* after each simulation tick; horn: held */
enum { HORN_TWO_TONE, HORN_BIG_AIR, HORN_JINGLE, HORN_QUACK, HORNS };
void sound_set_horn(int horn);       /* which horn sounds while it is held (the career's) */
void sound_horn_preview(void);       /* half a second of it (the garage) */
void sound_clunk(void);
void sound_coin(void);               /* a coin landing in the wallet */
void sound_chime(int grade);         /* 1 (OK) .. 4 (PERFECT) */
void sound_stop(void);

/* -soundtest: each sound in turn for the SOUND suite (a 440 Hz reference,
 * the horn, the beeper, the chime, the clunk, the thud, the screech, the
 * engine rising from idle to the red line). Call once a tick from tick 0;
 * returns 0 when it has finished. */
int sound_test_tick(uint32_t tick);

#endif
