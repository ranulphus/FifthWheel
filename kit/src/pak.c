/* pak.c - loading packs (see dgk/pak.h). */
#include "dgk/pak.h"
#include "dgk/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int dgk_pak_load(dgk_pak *p, const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint32_t i;
    memset(p, 0, sizeof *p);
    if (!f) {
        dgk_log("FW-ERROR pack %s: cannot open", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 32 || !(p->data = (uint8_t *)malloc((size_t)n)) || fread(p->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        dgk_pak_free(p);
        dgk_log("FW-ERROR pack %s: cannot read", path);
        return -1;
    }
    fclose(f);
    if (memcmp(p->data, "DGKP", 4) || (p->data[4] | p->data[5] << 8) != DGK_PAK_MAJOR ||
        rd32(p->data + 16) != (uint32_t)n) {
        dgk_log("FW-ERROR pack %s: not a version %d pack of %ld bytes", path, DGK_PAK_MAJOR, n);
        dgk_pak_free(p);
        return -1;
    }
    p->size = (uint32_t)n;
    p->nsections = rd32(p->data + 8);
    p->seed = rd32(p->data + 24);
    if (rd32(p->data + 12) + p->nsections * 16u > p->size ||
        dgk_crc32(0, p->data + rd32(p->data + 12), p->nsections * 16u) != rd32(p->data + 20)) {
        dgk_log("FW-ERROR pack %s: bad section table", path);
        dgk_pak_free(p);
        return -1;
    }
    p->table = (const uint32_t *)(p->data + rd32(p->data + 12));
    for (i = 0; i < p->nsections; i++) {
        uint32_t off = p->table[i * 4 + 1], size = p->table[i * 4 + 2];
        if (off + size > p->size || dgk_crc32(0, p->data + off, size) != p->table[i * 4 + 3]) {
            dgk_log("FW-ERROR pack %s: section %u damaged", path, (unsigned)i);
            dgk_pak_free(p);
            return -1;
        }
    }
    return 0;
}

void dgk_pak_free(dgk_pak *p)
{
    free(p->data);
    memset(p, 0, sizeof *p);
}

const void *dgk_pak_section(const dgk_pak *p, uint32_t fourcc, uint32_t *size)
{
    uint32_t i;
    for (i = 0; i < p->nsections; i++)
        if (p->table[i * 4] == fourcc) {
            if (size)
                *size = p->table[i * 4 + 2];
            return p->data + p->table[i * 4 + 1];
        }
    return NULL;
}
