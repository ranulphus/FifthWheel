/* dgk/base.h - integer types, arenas, hashes and a random number generator. */
#ifndef DGK_BASE_H
#define DGK_BASE_H

#include <stddef.h>
#include <stdint.h>

#if defined(__DJGPP__)
#  define DGK_DOS 1
#endif

#define DGK_UNUSED(x) ((void)(x))
#define DGK_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define DGK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define DGK_MAX(a, b) ((a) > (b) ? (a) : (b))
#define DGK_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

/* A bump allocator over one block: everything in it is freed together. */
typedef struct dgk_arena {
    uint8_t *base;
    size_t size, used, peak;
} dgk_arena;

int   dgk_arena_init(dgk_arena *a, size_t size);        /* 0 on success */
void *dgk_arena_alloc(dgk_arena *a, size_t n);           /* 16-byte aligned; NULL when full */
void  dgk_arena_reset(dgk_arena *a);
void  dgk_arena_free(dgk_arena *a);

uint32_t dgk_crc32(uint32_t crc, const void *p, size_t n); /* start with 0 */
uint32_t dgk_fnv1a(uint32_t h, const void *p, size_t n);   /* start with 2166136261 */

/* PCG32 (O'Neill): the same sequence on every platform. */
typedef struct dgk_rng {
    uint64_t state, inc;
} dgk_rng;

void     dgk_rng_seed(dgk_rng *r, uint64_t seed, uint64_t stream);
uint32_t dgk_rng_u32(dgk_rng *r);
uint32_t dgk_rng_below(dgk_rng *r, uint32_t n);          /* 0..n-1, unbiased */

#endif
