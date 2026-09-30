/* pakw.c - writing packs (see dgk/pak.h for the layout). */
#include "pakw.h"
#include "dgk/pak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SECTIONS 64

struct pakw {
    uint32_t seed, gen;
    int n;
    struct { uint32_t fourcc, size; uint8_t *data; } s[MAX_SECTIONS];
};

pakw *pakw_new(uint32_t seed, uint32_t generator_hash)
{
    pakw *w = (pakw *)calloc(1, sizeof *w);
    if (w) {
        w->seed = seed;
        w->gen = generator_hash;
    }
    return w;
}

void pakw_section(pakw *w, uint32_t fourcc, const void *data, uint32_t size)
{
    if (w->n == MAX_SECTIONS) {
        fprintf(stderr, "pakw: too many sections\n");
        exit(1);
    }
    w->s[w->n].fourcc = fourcc;
    w->s[w->n].size = size;
    w->s[w->n].data = (uint8_t *)malloc(size ? size : 1);
    memcpy(w->s[w->n].data, data, size);
    w->n++;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int pakw_write(pakw *w, const char *path)
{
    uint32_t off = 32, total, table_off, i;
    uint8_t *buf, *table;
    FILE *f;
    int ok;
    for (i = 0; i < (uint32_t)w->n; i++)
        off = ((off + 15) & ~15u) + w->s[i].size;
    table_off = (off + 15) & ~15u;
    total = table_off + (uint32_t)w->n * 16u;
    buf = (uint8_t *)calloc(1, total);
    table = buf + table_off;
    off = 32;
    for (i = 0; i < (uint32_t)w->n; i++) {
        off = (off + 15) & ~15u;
        memcpy(buf + off, w->s[i].data, w->s[i].size);
        put32(table + i * 16, w->s[i].fourcc);
        put32(table + i * 16 + 4, off);
        put32(table + i * 16 + 8, w->s[i].size);
        put32(table + i * 16 + 12, dgk_crc32(0, w->s[i].data, w->s[i].size));
        off += w->s[i].size;
        free(w->s[i].data);
    }
    memcpy(buf, "DGKP", 4);
    buf[4] = DGK_PAK_MAJOR; buf[5] = 0; buf[6] = DGK_PAK_MINOR; buf[7] = 0;
    put32(buf + 8, (uint32_t)w->n);
    put32(buf + 12, table_off);
    put32(buf + 16, total);
    put32(buf + 20, dgk_crc32(0, table, (uint32_t)w->n * 16u));
    put32(buf + 24, w->seed);
    put32(buf + 28, w->gen);
    f = fopen(path, "wb");
    ok = f && fwrite(buf, 1, total, f) == total;
    if (f)
        ok = fclose(f) == 0 && ok;
    free(buf);
    free(w);
    if (!ok)
        fprintf(stderr, "pakw: cannot write %s\n", path);
    return ok ? 0 : -1;
}
