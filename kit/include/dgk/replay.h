/* dgk/replay.h - recorded input: one fixed-size frame per simulation tick,
 * and a hash of the game's state every 60 ticks, so a playback that goes
 * somewhere else is caught at the first second it differs.
 *
 *   file: "DGKR", u16 version 1, u16 frame size, u32 ticks, u32 seed,
 *         then per tick the frame; per 60th tick also the u32 hash    */
#ifndef DGK_REPLAY_H
#define DGK_REPLAY_H

#include "dgk/base.h"

typedef struct dgk_replay dgk_replay;

dgk_replay *dgk_replay_record(int frame_size, uint32_t seed);
dgk_replay *dgk_replay_play(const char *path, int frame_size);   /* NULL if unreadable */
/* Recording: store the frame. Playing: fill it; 0 when the recording has ended. */
int  dgk_replay_frame(dgk_replay *r, void *frame);
/* Every tick, with the state after the tick: stored every 60th (recording),
 * compared (playing; returns 0 on a mismatch and logs it). */
int  dgk_replay_hash(dgk_replay *r, uint32_t tick, uint32_t hash);
int  dgk_replay_save(dgk_replay *r, const char *path);            /* recording */
uint32_t dgk_replay_seed(const dgk_replay *r);
void dgk_replay_free(dgk_replay *r);

#endif
