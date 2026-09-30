/* dgk/bench.h - timing runs in DOSBench's record format (DOSBench
 * docs/methodology.md): an H line per run and mode, a T line per test,
 * appended to <dir>/RESULTS.TXT (C:\OUT on DOS, out/ elsewhere) and sent as
 * "HX-STAT bench ..." lines. Frame times come from the platform's clock
 * (uclock on DOS: 0.84 us); the first frames of a test are warm-up. */
#ifndef DGK_BENCH_H
#define DGK_BENCH_H

#include "dgk/base.h"

#define DGK_BENCH_MAX_FRAMES 8192

void dgk_bench_run(const char *prog, const char *ver);   /* once per run: the H line */
void dgk_bench_begin(const char *test, int warmup_frames);
void dgk_bench_frame(uint64_t frame_us, uint32_t tris);  /* each frame, swap included */
void dgk_bench_end(const char *status, const char *notes);   /* the T line */

#endif
