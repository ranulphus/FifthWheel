/* music.c - the music player (see music.h): a tune's MIDI file read into its messages, timed in
 * microseconds, which go to dgk's music (dgk/midi.h) a little ahead of their time. */
#include "music.h"
#include "dgk/app.h"
#include "dgk/base.h"
#include "dgk/log.h"
#include "dgk/midi.h"
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 16384
#define DRIVE_FIRST 2                /* music_tunes[2..]: the driving tunes */
#define DRIVE_LOOPS 1                /* each played this many times before the next (once: the owner, 2026-11-26) */
#define AHEAD_US 150000              /* messages go this far ahead of their time (a slow frame's worth) */

/* ---- the tune ---------------------------------------------------------------- */

typedef struct mev {
    uint32_t us;                     /* from the tune's start */
    uint8_t st, d1, d2;
} mev;

static mev evs[MAX_EVENTS];
static int nev, pos, tune = -1, want = MUSIC_OFF, drive, loops, device = DGK_MIDI_OFF;
static uint32_t loop_us;
static uint64_t t0;                  /* the tune's start, this time round, on now_us's clock */

typedef struct tev {
    uint32_t tick;
    int order;
    uint8_t st, d1, d2;
    uint32_t tempo;                  /* st 0xFF: a tempo, microseconds a beat */
} tev;

static int cmp_tev(const void *a, const void *b)
{
    const tev *x = a, *y = b;
    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A standard MIDI file (format 0 or 1, ticks a beat) into evs[], timed in microseconds; 0, or -1 if it is not
 * one. The loop: the longest track's end. */
static int load(const unsigned char *d, unsigned int size)
{
    static tev t[MAX_EVENTS];
    unsigned int at = 14, i;
    int nt = 0, tracks, division, k;
    uint32_t end = 0, tempo = 500000, last_tick = 0;
    double us = 0;
    if (size < 14 || memcmp(d, "MThd", 4) || rd32(d + 4) != 6)
        return -1;
    tracks = d[10] << 8 | d[11];
    division = d[12] << 8 | d[13];
    if (division <= 0 || division >= 0x8000)
        return -1;
    for (k = 0; k < tracks && at + 8 <= size; k++) {
        uint32_t len = rd32(d + at + 4), p = at + 8, stop = at + 8 + len, tick = 0;
        uint8_t run = 0;
        if (memcmp(d + at, "MTrk", 4) || stop > size)
            return -1;
        while (p < stop) {
            uint32_t delta = 0;
            uint8_t st;
            do
                delta = delta << 7 | (d[p] & 0x7F);
            while ((d[p++] & 0x80) && p < stop);
            tick += delta;
            st = d[p];
            if (st & 0x80)
                p++;
            else
                st = run;
            if (st == 0xFF) {                        /* a meta event: the tempo kept, the rest passed over */
                uint8_t type = d[p++];
                uint32_t mlen = 0;
                do
                    mlen = mlen << 7 | (d[p] & 0x7F);
                while ((d[p++] & 0x80) && p < stop);
                if (type == 0x51 && mlen == 3 && nt < MAX_EVENTS) {
                    t[nt].tick = tick;
                    t[nt].order = nt;
                    t[nt].st = 0xFF;
                    t[nt].tempo = (uint32_t)d[p] << 16 | (uint32_t)d[p + 1] << 8 | d[p + 2];
                    nt++;
                }
                if (type == 0x2F && tick > end)
                    end = tick;
                p += mlen;
            } else if (st == 0xF0 || st == 0xF7) {   /* sysex: passed over */
                uint32_t slen = 0;
                do
                    slen = slen << 7 | (d[p] & 0x7F);
                while ((d[p++] & 0x80) && p < stop);
                p += slen;
            } else {
                int n = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 1 : 2;
                run = st;
                if (nt < MAX_EVENTS) {
                    t[nt].tick = tick;
                    t[nt].order = nt;
                    t[nt].st = st;
                    t[nt].d1 = d[p];
                    t[nt].d2 = n == 2 ? d[p + 1] : 0;
                    nt++;
                }
                p += (uint32_t)n;
            }
        }
        if (tick > end)
            end = tick;
        at = stop;
    }
    qsort(t, (size_t)nt, sizeof t[0], cmp_tev);
    nev = 0;
    for (i = 0; i < (unsigned int)nt; i++) {
        us += (double)(t[i].tick - last_tick) * tempo / division;
        last_tick = t[i].tick;
        if (t[i].st == 0xFF) {
            tempo = t[i].tempo;
            continue;
        }
        evs[nev].us = (uint32_t)us;
        evs[nev].st = t[i].st;
        evs[nev].d1 = t[i].d1;
        evs[nev].d2 = t[i].d2;
        nev++;
    }
    us += (double)(end - last_tick) * tempo / division;
    loop_us = (uint32_t)us;
    return nev > 0 && loop_us > 0 ? 0 : -1;
}

/* ---- playing -------------------------------------------------------------- */

/* The game's clock (in a -fixed test, its ticks: the same messages at the same times every run). */
static uint64_t now_us(void)
{
    return dgk_app.fixed ? (uint64_t)dgk_app.ticks * 1000000u / DGK_TICK_HZ : dgk_now_us();
}

static uint32_t ms(uint64_t us)
{
    return (uint32_t)(us / 1000u);
}

/* Every channel as General MIDI starts it (each tune sets its programs and volumes at its start). */
static void reset_channels(uint64_t at)
{
    int c;
    for (c = 0; c < 16; c++) {
        dgk_midi_send(ms(at), (uint8_t)(0xB0 | c), 121, 0);   /* the controllers reset */
        dgk_midi_send(ms(at), (uint8_t)(0xE0 | c), 0, 64);    /* no bend */
    }
}

/* Tune t from `at` on (the last one's end, so the turns follow seamlessly; else now, after silence). */
static void start(int t, uint64_t at)
{
    tune = -1;
    if (t < 0 || t >= music_ntunes || load(music_tunes[t].data, music_tunes[t].size) != 0)
        return;
    tune = t;
    pos = 0;
    loops = 0;
    t0 = at;
    reset_channels(at);
    dgk_log("FW-MUSIC %s: %d events, %.1f s a loop", music_tunes[t].name, nev, loop_us / 1e6);
}

static void start_now(int t)
{
    dgk_midi_panic();
    start(t, now_us());
}

void music_init(void)
{
    device = dgk_app.sound ? dgk_midi_open(DGK_MIDI_AUTO) : DGK_MIDI_OFF;
    dgk_log("FW-MUSIC %s", device == DGK_MIDI_OFF ? "nothing to play it on: no music" : dgk_midi_name());
}

void music_want(int what)
{
    if (what == want)
        return;
    want = what;
    if (device == DGK_MIDI_OFF)
        return;
    switch (what) {
    case MUSIC_TITLE:
        start_now(0);
        break;
    case MUSIC_GARAGE:
        start_now(1);
        break;
    case MUSIC_DRIVING:
        start_now(DRIVE_FIRST + drive % DGK_MAX(music_ntunes - DRIVE_FIRST, 1));
        break;
    default:
        dgk_midi_panic();
        tune = -1;
        break;
    }
}

int music_play(const char *name)
{
    int t;
    for (t = 0; t < music_ntunes; t++)
        if (!strcmp(music_tunes[t].name, name))
            break;
    if (t == music_ntunes)
        return 0;
    want = -1;                               /* (none of MUSIC_*: no turns) */
    if (device != DGK_MIDI_OFF)
        start_now(t);
    return 1;
}

void music_volume(int l)
{
    dgk_midi_volume(DGK_CLAMP(l, 0, 10) * 10);
}

void music_update(void)
{
    uint64_t now;
    if (device == DGK_MIDI_OFF)
        return;
    now = now_us();
    while (tune >= 0) {
        if (pos < nev) {
            const mev *e = &evs[pos];
            if (t0 + e->us > now + AHEAD_US)
                break;
            dgk_midi_send(ms(t0 + e->us), e->st, e->d1, e->d2);
            pos++;
            continue;
        }
        /* The end: round again from it, or (driving, played enough) the next driving tune. */
        if (t0 + loop_us > now + AHEAD_US)
            break;
        t0 += loop_us;
        pos = 0;
        if (want == MUSIC_DRIVING && ++loops >= DRIVE_LOOPS && music_ntunes > DRIVE_FIRST + 1) {
            drive++;
            start(DRIVE_FIRST + drive % (music_ntunes - DRIVE_FIRST), t0);
        }
    }
    dgk_midi_service(ms(now));
}

void music_shutdown(void)
{
    if (device == DGK_MIDI_OFF)
        return;
    dgk_midi_close();
    device = DGK_MIDI_OFF;
    tune = -1;
}

double music_loop_s(void)
{
    return tune >= 0 ? loop_us / 1e6 : 0;
}

const char *music_playing(void)
{
    return tune >= 0 ? music_tunes[tune].name : NULL;
}
