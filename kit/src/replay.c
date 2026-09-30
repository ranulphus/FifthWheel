/* replay.c - recorded input (see dgk/replay.h). */
#include "dgk/replay.h"
#include "dgk/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dgk_replay {
    int frame_size, playing;
    uint32_t seed, ticks, pos;
    uint8_t *data;              /* the stream: frames, a hash after every 60th */
    uint32_t size, cap;
};

static void put(dgk_replay *r, const void *p, uint32_t n)
{
    if (r->size + n > r->cap) {
        r->cap = r->cap ? r->cap * 2 : 65536;
        while (r->cap < r->size + n)
            r->cap *= 2;
        r->data = (uint8_t *)realloc(r->data, r->cap);
    }
    memcpy(r->data + r->size, p, n);
    r->size += n;
}

dgk_replay *dgk_replay_record(int frame_size, uint32_t seed)
{
    dgk_replay *r = (dgk_replay *)calloc(1, sizeof *r);
    r->frame_size = frame_size;
    r->seed = seed;
    return r;
}

dgk_replay *dgk_replay_play(const char *path, int frame_size)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[16];
    dgk_replay *r;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f) - 16;
    fseek(f, 0, SEEK_SET);
    if (n < 0 || fread(h, 1, 16, f) != 16 || memcmp(h, "DGKR", 4) || h[4] != 1 || (h[6] | h[7] << 8) != frame_size) {
        fclose(f);
        dgk_log("FW-ERROR replay %s: not a version 1 replay of %d-byte frames", path, frame_size);
        return NULL;
    }
    r = (dgk_replay *)calloc(1, sizeof *r);
    r->frame_size = frame_size;
    r->playing = 1;
    memcpy(&r->ticks, h + 8, 4);
    memcpy(&r->seed, h + 12, 4);
    r->data = (uint8_t *)malloc((size_t)n + 1);
    r->size = (uint32_t)fread(r->data, 1, (size_t)n, f);
    fclose(f);
    return r;
}

int dgk_replay_frame(dgk_replay *r, void *frame)
{
    if (!r->playing) {
        put(r, frame, (uint32_t)r->frame_size);
        r->ticks++;
        return 1;
    }
    if (r->pos + (uint32_t)r->frame_size > r->size)
        return 0;
    memcpy(frame, r->data + r->pos, (size_t)r->frame_size);
    r->pos += (uint32_t)r->frame_size;
    return 1;
}

int dgk_replay_hash(dgk_replay *r, uint32_t tick, uint32_t hash)
{
    uint32_t stored;
    if ((tick + 1) % 60)
        return 1;
    if (!r->playing) {
        put(r, &hash, 4);
        return 1;
    }
    if (r->pos + 4 > r->size)
        return 1;
    memcpy(&stored, r->data + r->pos, 4);
    r->pos += 4;
    if (stored != hash) {
        dgk_log("FW-DESYNC tick=%lu recorded=%08lx now=%08lx", (unsigned long)tick, (unsigned long)stored,
                (unsigned long)hash);
        return 0;
    }
    return 1;
}

int dgk_replay_save(dgk_replay *r, const char *path)
{
    FILE *f = fopen(path, "wb");
    uint8_t h[16];
    int ok;
    if (!f)
        return -1;
    memcpy(h, "DGKR", 4);
    h[4] = 1; h[5] = 0;
    h[6] = (uint8_t)r->frame_size; h[7] = (uint8_t)(r->frame_size >> 8);
    memcpy(h + 8, &r->ticks, 4);
    memcpy(h + 12, &r->seed, 4);
    ok = fwrite(h, 1, 16, f) == 16 && fwrite(r->data, 1, r->size, f) == r->size;
    return fclose(f) == 0 && ok ? 0 : -1;
}

uint32_t dgk_replay_seed(const dgk_replay *r)
{
    return r->seed;
}

void dgk_replay_free(dgk_replay *r)
{
    if (r) {
        free(r->data);
        free(r);
    }
}
