/* probe.h - the performance probe (plan F2, DOSBench test FWP): synthetic
 * scenes through the game's camera and GL paths, timed as DOSBench tests,
 * to measure what the budget model (docs/perf.md) needs on each machine:
 * cost per triangle, per draw call, of texturing, of each submission path. */
#ifndef FW_PROBE_H
#define FW_PROBE_H

int  probe_init(int quick);
void probe_draw(void);          /* one frame; ends the loop after the last case */

#endif
