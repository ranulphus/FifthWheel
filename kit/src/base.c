/* base.c - arenas, CRC-32, FNV-1a, PCG32. */
#include "dgk/base.h"
#include <stdlib.h>

int dgk_arena_init(dgk_arena *a, size_t size)
{
    a->base = (uint8_t *)malloc(size);
    a->size = a->base ? size : 0;
    a->used = a->peak = 0;
    return a->base ? 0 : -1;
}

void *dgk_arena_alloc(dgk_arena *a, size_t n)
{
    size_t at = (a->used + 15) & ~(size_t)15;
    if (at + n > a->size)
        return NULL;
    a->used = at + n;
    if (a->used > a->peak)
        a->peak = a->used;
    return a->base + at;
}

void dgk_arena_reset(dgk_arena *a)
{
    a->used = 0;
}

void dgk_arena_free(dgk_arena *a)
{
    free(a->base);
    a->base = NULL;
    a->size = a->used = 0;
}

uint32_t dgk_crc32(uint32_t crc, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    crc = ~crc;
    while (n--) {
        int k;
        crc ^= *b++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

uint32_t dgk_fnv1a(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

void dgk_rng_seed(dgk_rng *r, uint64_t seed, uint64_t stream)
{
    r->state = 0;
    r->inc = (stream << 1) | 1;
    dgk_rng_u32(r);
    r->state += seed;
    dgk_rng_u32(r);
}

uint32_t dgk_rng_u32(dgk_rng *r)
{
    uint64_t old = r->state;
    uint32_t xorshifted, rot;
    r->state = old * 6364136223846793005ULL + r->inc;
    xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27);
    rot = (uint32_t)(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

uint32_t dgk_rng_below(dgk_rng *r, uint32_t n)
{
    uint32_t threshold = (0u - n) % n;
    for (;;) {
        uint32_t x = dgk_rng_u32(r);
        if (x >= threshold)
            return x % n;
    }
}
