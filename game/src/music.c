/* music.c - the music player (see music.h). */
#include "music.h"
#include "opl.h"
#include "dgk/app.h"
#include "dgk/base.h"
#include "dgk/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VOICES 6                     /* melodic channels 0-5; 6-8 are the drums' */
#define MAX_EVENTS 16384
#define DRIVE_FIRST 2                /* music_tunes[2..]: the driving tunes */
#define DRIVE_LOOPS 2                /* each played this many times before the next */

/* ---- the instruments ------------------------------------------------------ */

/* An operator's registers as written: 20h (AM, vibrato, sustained, key scaling, multiple), 40h (key level
 * scaling, total level), 60h (attack, decay), 80h (sustain level, release), E0h (waveform). */
typedef struct fm_op {
    uint8_t r20, r40, r60, r80, re0;
} fm_op;

typedef struct fm_patch {
    const char *name;
    fm_op mod, car;
    uint8_t rc0;                     /* feedback (bits 1-3), additive (bit 0) */
} fm_patch;

enum { P_PIANO, P_EPIANO, P_VIBES, P_MARIMBA, P_GLOCK, P_ORGAN, P_REED, P_CLARINET, P_FLUTE, P_TRUMPET,
       P_TROMBONE, P_SAX, P_STRINGS, P_GUITAR, P_BANJO, P_BASS, P_TUBA, PATCHES };

static const fm_patch patches[PATCHES] = {
    { "piano", { 0x01, 0x5A, 0xF3, 0x55, 0 }, { 0x01, 0x00, 0xF2, 0x75, 0 }, 0x06 },
    { "epiano", { 0x01, 0x24, 0xF5, 0x56, 0 }, { 0x01, 0x00, 0xF2, 0x66, 0 }, 0x00 },
    { "vibes", { 0x84, 0x35, 0xF4, 0xF5, 0 }, { 0x81, 0x00, 0xF3, 0x55, 0 }, 0x00 },
    { "marimba", { 0x04, 0x2A, 0xF7, 0xF6, 0 }, { 0x01, 0x00, 0xF6, 0xF6, 0 }, 0x00 },
    { "glock", { 0x07, 0x30, 0xF6, 0xF5, 0 }, { 0x04, 0x00, 0xF5, 0xF5, 0 }, 0x00 },
    { "organ", { 0x22, 0x10, 0xF0, 0x07, 0 }, { 0x21, 0x00, 0xF0, 0x07, 0 }, 0x01 },
    { "reed", { 0x62, 0x18, 0x92, 0x17, 1 }, { 0x61, 0x00, 0x96, 0x17, 0 }, 0x08 },
    { "clarinet", { 0x22, 0x16, 0x76, 0x17, 0 }, { 0x21, 0x00, 0x75, 0x17, 0 }, 0x00 },
    { "flute", { 0x61, 0x24, 0x75, 0x17, 0 }, { 0x61, 0x00, 0x65, 0x17, 0 }, 0x0C },
    { "trumpet", { 0x21, 0x12, 0x84, 0x24, 0 }, { 0x21, 0x00, 0x82, 0x15, 0 }, 0x0C },
    { "trombone", { 0x21, 0x18, 0x73, 0x23, 0 }, { 0x21, 0x00, 0x73, 0x15, 0 }, 0x0A },
    { "sax", { 0x31, 0x14, 0x86, 0x25, 1 }, { 0x21, 0x00, 0x76, 0x17, 0 }, 0x0A },
    { "strings", { 0x61, 0x1C, 0x54, 0x14, 0 }, { 0x61, 0x00, 0x53, 0x15, 0 }, 0x06 },
    { "guitar", { 0x01, 0x1E, 0xF4, 0x95, 0 }, { 0x01, 0x00, 0xF3, 0x96, 0 }, 0x08 },
    { "banjo", { 0x02, 0x16, 0xF6, 0xF6, 0 }, { 0x01, 0x00, 0xF5, 0xF6, 0 }, 0x0A },
    { "bass", { 0x01, 0x1C, 0xF4, 0x36, 0 }, { 0x01, 0x00, 0xF3, 0x78, 0 }, 0x08 },
    { "tuba", { 0x21, 0x14, 0xA3, 0x25, 0 }, { 0x21, 0x00, 0xA3, 0x16, 0 }, 0x0A },
};

/* A General MIDI program's nearest instrument here. */
static int patch_for(int prog)
{
    if (prog <= 3 || prog == 6 || prog == 7)
        return P_PIANO;
    if (prog <= 5)
        return P_EPIANO;
    if (prog == 11)
        return P_VIBES;
    if (prog <= 10 || prog == 14)
        return P_GLOCK;
    if (prog <= 15)
        return P_MARIMBA;
    if (prog <= 20)
        return P_ORGAN;
    if (prog <= 23)
        return P_REED;
    if (prog <= 31)
        return P_GUITAR;
    if (prog <= 39)
        return P_BASS;
    if (prog <= 55)
        return P_STRINGS;
    if (prog == 57 || prog == 58)
        return prog == 58 ? P_TUBA : P_TROMBONE;
    if (prog <= 63)
        return P_TRUMPET;
    if (prog <= 67)
        return P_SAX;
    if (prog <= 71)
        return P_CLARINET;
    if (prog <= 79)
        return P_FLUTE;
    if (prog <= 103)
        return prog < 88 ? P_ORGAN : P_STRINGS;
    if (prog == 105 || prog == 106)
        return P_BANJO;
    if (prog <= 111)
        return P_GUITAR;
    return P_MARIMBA;
}

/* ---- the tune ---------------------------------------------------------------- */

typedef struct mev {
    uint32_t us;                     /* from the tune's start */
    uint8_t st, d1, d2;
} mev;

static mev evs[MAX_EVENTS];
static int nev, pos, tune = -1, want = MUSIC_OFF, drive, loops, level = 7, chip = OPL_NONE;
static uint32_t loop_us;
static uint64_t t0;

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

/* ---- the chip's voices ----------------------------------------------------- */

static const int mod_off[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

typedef struct voice {
    int on, held, ch, note, patch, vel;
    uint32_t age;
} voice;

typedef struct channel {
    int patch, vol, expr, sustain, bend;     /* bend: -8192 .. 8191 (two semitones) */
} channel;

static voice voices[VOICES];
static channel chans[16];
static uint32_t age;

static void fnum_block(double freq, int *fnum, int *block)
{
    int b;
    for (b = 0; b < 8; b++) {
        double f = freq * (double)(1 << (20 - b)) / 49716.0;
        if (f < 1023.5 || b == 7) {
            *fnum = (int)DGK_CLAMP(f + 0.5, 0.0, 1023.0);
            *block = b;
            return;
        }
    }
}

/* Attenuation in the chip's 0.75 dB steps for a velocity, the channel's volume and expression (20 log10 of
 * each: the chip is quiet beside the Sound Blaster's samples, so no steeper), and the music's level (2 dB a
 * step below 10; 0 silent). */
static int attenuation(int vel, const channel *c)
{
    double g = (vel / 127.0) * (c->vol / 127.0) * (c->expr / 127.0), db;
    if (g <= 0.0001 || level <= 0)
        return 63;
    db = 20.0 * log10(g) - (10 - level) * 2.0;
    return (int)DGK_CLAMP(-db / 0.75 + 0.5, 0.0, 63.0);
}

static void voice_level(int v)
{
    const fm_patch *p = &patches[voices[v].patch];
    int a = attenuation(voices[v].vel, &chans[voices[v].ch]), m = mod_off[v];
    int car_tl = DGK_MIN((p->car.r40 & 63) + a, 63), mod_tl = DGK_MIN((p->mod.r40 & 63) + ((p->rc0 & 1) ? a : 0), 63);
    opl_write(0x40 + m + 3, (p->car.r40 & 0xC0) | car_tl);
    opl_write(0x40 + m, (p->mod.r40 & 0xC0) | mod_tl);
}

static void voice_pitch(int v, int keyon)
{
    double semis = voices[v].note + chans[voices[v].ch].bend * 2.0 / 8192.0;
    int fnum, block;
    fnum_block(440.0 * pow(2.0, (semis - 69.0) / 12.0), &fnum, &block);
    opl_write(0xA0 + v, fnum & 255);
    opl_write(0xB0 + v, (keyon ? 0x20 : 0) | block << 2 | (fnum >> 8));
}

static void voice_pitch(int v, int keyon);

static void voice_off(int v)
{
    voices[v].on = voices[v].held = 0;
    voice_pitch(v, 0);                               /* the key up, its pitch kept for the release */
}

static void program(int v, int patch)
{
    const fm_patch *p = &patches[patch];
    int m = mod_off[v];
    opl_write(0x20 + m, p->mod.r20);
    opl_write(0x60 + m, p->mod.r60);
    opl_write(0x80 + m, p->mod.r80);
    opl_write(0xE0 + m, p->mod.re0);
    opl_write(0x20 + m + 3, p->car.r20);
    opl_write(0x60 + m + 3, p->car.r60);
    opl_write(0x80 + m + 3, p->car.r80);
    opl_write(0xE0 + m + 3, p->car.re0);
    opl_write(0xC0 + v, p->rc0);
    voices[v].patch = patch;
}

static void note_on(int ch, int note, int vel)
{
    int v, best = -1;
    /* The same note again: that voice; else a free one, the longest off; else the oldest note stops for it. */
    for (v = 0; v < VOICES; v++)
        if (voices[v].on && voices[v].ch == ch && voices[v].note == note)
            best = v;
    for (v = 0; v < VOICES && best < 0; v++)
        if (!voices[v].on)
            best = v;
    if (best >= 0 && voices[best].note != note)
        for (v = 0; v < VOICES; v++)
            if (!voices[v].on && voices[v].age < voices[best].age)
                best = v;
    if (best < 0) {
        best = 0;
        for (v = 1; v < VOICES; v++)
            if (voices[v].age < voices[best].age)
                best = v;
    }
    if (voices[best].on)
        voice_off(best);
    if (voices[best].patch != chans[ch].patch || !voices[best].age)
        program(best, chans[ch].patch);
    voices[best].on = 1;
    voices[best].held = 0;
    voices[best].ch = ch;
    voices[best].note = note;
    voices[best].vel = vel;
    voices[best].age = ++age;
    voice_level(best);
    voice_pitch(best, 1);
}

static void note_off(int ch, int note)
{
    int v;
    for (v = 0; v < VOICES; v++)
        if (voices[v].on && voices[v].ch == ch && voices[v].note == note) {
            if (chans[ch].sustain)
                voices[v].held = 1;
            else {
                voices[v].on = 0;
                voices[v].age = ++age;
                voice_pitch(v, 0);
            }
        }
}

/* ---- the drums: the chip's rhythm mode -------------------------------------- */

enum { D_BD = 0x10, D_SD = 0x08, D_TOM = 0x04, D_CYM = 0x02, D_HH = 0x01 };

static int rhythm_bits = 0x20;

static void drums_setup(void)
{
    /* The bass drum (channel 6's two operators), the hi-hat and snare (channel 7's), tom and cymbal (8's),
     * and their pitches. */
    static const struct { int off; fm_op op; } d[] = {
        { 16, { 0x01, 0x0D, 0xF8, 0x6F, 0 } },   /* BD modulator */
        { 19, { 0x01, 0x00, 0xF6, 0x67, 0 } },   /* BD carrier */
        { 17, { 0x01, 0x04, 0xF9, 0xF7, 0 } },   /* hi-hat */
        { 20, { 0x01, 0x02, 0xF8, 0xB6, 0 } },   /* snare */
        { 18, { 0x02, 0x04, 0xF7, 0x75, 0 } },   /* tom */
        { 21, { 0x01, 0x06, 0xF4, 0x84, 0 } },   /* cymbal */
    };
    size_t i;
    for (i = 0; i < sizeof d / sizeof d[0]; i++) {
        opl_write(0x20 + d[i].off, d[i].op.r20);
        opl_write(0x40 + d[i].off, d[i].op.r40);
        opl_write(0x60 + d[i].off, d[i].op.r60);
        opl_write(0x80 + d[i].off, d[i].op.r80);
        opl_write(0xE0 + d[i].off, d[i].op.re0);
    }
    opl_write(0xC6, 0x08);
    opl_write(0xA6, 580 & 255);                      /* 55 Hz */
    opl_write(0xB6, 1 << 2 | 580 >> 8);
    opl_write(0xA7, 527 & 255);                      /* about 200 Hz */
    opl_write(0xB7, 3 << 2 | 527 >> 8);
    opl_write(0xA8, 633 & 255);                      /* about 120 Hz */
    opl_write(0xB8, 2 << 2 | 633 >> 8);
    rhythm_bits = 0x20;
    opl_write(0xBD, rhythm_bits);
}

static void drum(int note, int vel)
{
    int bit, off, base, a;
    channel c = { 0, 127, 127, 0, 0 };
    if (note == 35 || note == 36)
        bit = D_BD, off = 19, base = 0x00;
    else if (note >= 37 && note <= 40)
        bit = D_SD, off = 20, base = 0x02;
    else if (note == 42 || note == 44 || note == 46)
        bit = D_HH, off = 17, base = note == 46 ? 0x02 : 0x06;
    else if (note == 41 || note == 43 || note == 45 || note == 47 || note == 48 || note == 50)
        bit = D_TOM, off = 18, base = 0x04;
    else if (note == 49 || note == 51 || note == 52 || note == 53 || note == 55 || note == 57 || note == 59)
        bit = D_CYM, off = 21, base = note == 51 || note == 53 || note == 59 ? 0x0C : 0x06;
    else
        return;
    c.vol = chans[9].vol;
    c.expr = chans[9].expr;
    a = attenuation(vel, &c);
    opl_write(0x40 + off, DGK_MIN(base + a, 63));
    opl_write(0xBD, rhythm_bits & ~bit);             /* struck again: up, then down */
    rhythm_bits |= bit;
    opl_write(0xBD, rhythm_bits);
    rhythm_bits &= ~bit;                             /* (the next strike's "up" is this write) */
}

/* ---- playing ------------------------------------------------------------------ */

static void all_off(void)
{
    int v;
    for (v = 0; v < VOICES; v++)
        if (voices[v].on)
            voice_off(v);
    if (chip != OPL_NONE)
        opl_write(0xBD, 0x20);
}

static void reset_channels(void)
{
    int c;
    for (c = 0; c < 16; c++) {
        chans[c].patch = P_PIANO;
        chans[c].vol = 100;
        chans[c].expr = 127;
        chans[c].sustain = 0;
        chans[c].bend = 0;
    }
}

static uint64_t now_us(void)
{
    /* A test's -fixed: its ticks (the same notes every run); else the clock. */
    return dgk_app.fixed ? (uint64_t)dgk_app.ticks * 1000000u / DGK_TICK_HZ : dgk_now_us();
}

static void dispatch(const mev *e)
{
    int ch = e->st & 15, i;
    switch (e->st & 0xF0) {
    case 0x90:                                       /* (velocity 0: a note off) */
        if (e->d2 && ch == 9)
            drum(e->d1, e->d2);
        else if (e->d2)
            note_on(ch, e->d1, e->d2);
        else if (ch != 9)
            note_off(ch, e->d1);
        break;
    case 0x80:
        if (ch != 9)
            note_off(ch, e->d1);
        break;
    case 0xB0:
        if (e->d1 == 7)
            chans[ch].vol = e->d2;
        else if (e->d1 == 11)
            chans[ch].expr = e->d2;
        else if (e->d1 == 64) {
            chans[ch].sustain = e->d2 >= 64;
            if (!chans[ch].sustain)
                for (i = 0; i < VOICES; i++)
                    if (voices[i].on && voices[i].held && voices[i].ch == ch) {
                        voices[i].on = voices[i].held = 0;
                        voice_pitch(i, 0);
                    }
        } else if (e->d1 == 123)
            all_off();
        break;
    case 0xC0:
        chans[ch].patch = patch_for(e->d1);
        break;
    case 0xE0:
        chans[ch].bend = (e->d1 | e->d2 << 7) - 8192;
        for (i = 0; i < VOICES; i++)
            if (voices[i].on && voices[i].ch == ch)
                voice_pitch(i, 1);
        break;
    default:
        break;
    }
}

static void start(int t)
{
    all_off();
    reset_channels();
    memset(voices, 0, sizeof voices);
    tune = -1;
    if (t < 0 || t >= music_ntunes || load(music_tunes[t].data, music_tunes[t].size) != 0)
        return;
    tune = t;
    pos = 0;
    loops = 0;
    t0 = now_us();
    dgk_log("FW-MUSIC %s: %d events, %.1f s a loop", music_tunes[t].name, nev, loop_us / 1e6);
}

void music_init(void)
{
    chip = dgk_app.sound ? opl_open() : OPL_NONE;
    dgk_log("FW-MUSIC %s", chip == OPL_CHIP3 ? "an OPL3 (as an OPL2)" : chip == OPL_CHIP2 ? "an OPL2"
                           : chip == OPL_EMULATED ? "an emulated OPL2" : "no FM chip: no music");
    if (chip != OPL_NONE)
        drums_setup();
}

void music_want(int what)
{
    if (what == want)
        return;
    want = what;
    if (chip == OPL_NONE)
        return;
    switch (what) {
    case MUSIC_TITLE:
        start(0);
        break;
    case MUSIC_GARAGE:
        start(1);
        break;
    case MUSIC_DRIVING:
        start(DRIVE_FIRST + drive % DGK_MAX(music_ntunes - DRIVE_FIRST, 1));
        break;
    default:
        all_off();
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
    if (chip != OPL_NONE)
        start(t);
    return 1;
}

void music_volume(int l)
{
    int v;
    level = DGK_CLAMP(l, 0, 10);
    for (v = 0; v < VOICES; v++)
        if (voices[v].on)
            voice_level(v);
}

void music_update(void)
{
    uint64_t el;
    if (chip == OPL_NONE || tune < 0)
        return;
    el = now_us() - t0;
    while (pos < nev && evs[pos].us <= el)
        dispatch(&evs[pos++]);
    if (pos >= nev && el >= loop_us) {
        /* The end: round again, or (driving, played enough) the next driving tune. */
        t0 += loop_us;
        pos = 0;
        if (want == MUSIC_DRIVING && ++loops >= DRIVE_LOOPS && music_ntunes > DRIVE_FIRST + 1) {
            drive++;
            start(DRIVE_FIRST + drive % (music_ntunes - DRIVE_FIRST));
        }
    }
}

void music_shutdown(void)
{
    if (chip == OPL_NONE)
        return;
    all_off();
    opl_close();
    chip = OPL_NONE;
    tune = -1;
}

const char *music_playing(void)
{
    return tune >= 0 ? music_tunes[tune].name : NULL;
}
