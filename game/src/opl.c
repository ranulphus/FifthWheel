/* opl.c - the FM chip, real or emulated (see opl.h). */
#include "opl.h"
#include "dgk/app.h"
#include "dgk/log.h"
#include "dgk/mix.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t reg[256];
static int kind = OPL_NONE;
static FILE *wlog;                   /* FW_OPL_LOG: every register written, microseconds (tests of the player) */
static unsigned long wlog_us(void);

#ifdef DGK_DOS
#include <pc.h>
#include <signal.h>

#define ADLIB 0x388

/* A register: its address, then (after the chip's 3.3 us: status reads, about a microsecond each on the ISA
 * bus) its value; and the next no sooner than the chip can take it, timed by the clock (status reads may take
 * no time at all in an emulator): an OPL2 23 us after a value, an OPL3 under 2 us: 25 us and 5 us. */
static int write_gap = 25;
static uint64_t last_write;

static void chip_write(int r, int v)
{
    int i;
    while (dgk_now_us() - last_write < (uint64_t)write_gap)
        ;
    outportb(ADLIB, (unsigned char)r);
    for (i = 0; i < 6; i++)
        inportb(ADLIB);
    outportb(ADLIB + 1, (unsigned char)v);
    last_write = dgk_now_us();
}

/* The AdLib test: timer 1 started, its flag set 80 us on; an OPL3's status has bits 1-2 clear. */
static int chip_detect(void)
{
    int s1, s2, i;
    chip_write(4, 0x60);
    chip_write(4, 0x80);
    s1 = inportb(ADLIB);
    chip_write(2, 0xFF);
    chip_write(4, 0x21);
    for (i = 0; i < 200; i++)
        inportb(ADLIB);
    s2 = inportb(ADLIB);
    chip_write(4, 0x60);
    chip_write(4, 0x80);
    if ((s1 & 0xE0) != 0x00 || (s2 & 0xE0) != 0xC0)
        return OPL_NONE;
    return (s2 & 0x06) == 0 ? OPL_CHIP3 : OPL_CHIP2;
}

/* Out of the program any way (exit, Ctrl-C, a fault): the chip silent, then on as before (DOS-GL's hooks too:
 * each chains to the one it found). */
static void (*prev_sig[4])(int);
static const int sigs[4] = { SIGINT, SIGILL, SIGSEGV, SIGFPE };
static int hooked;

static void silence(void)
{
    int r;
    for (r = 0xB0; r <= 0xB8; r++)
        chip_write(r, 0);
    chip_write(0xBD, 0);
    for (r = 0x40; r <= 0x55; r++)
        chip_write(r, 0x3F);
}

static void on_signal(int sig)
{
    int i;
    silence();
    for (i = 0; i < 4; i++)
        if (sigs[i] == sig) {
            signal(sig, prev_sig[i] ? prev_sig[i] : SIG_DFL);
            raise(sig);
            return;
        }
}

static void hook(void)
{
    int i;
    if (hooked)
        return;
    atexit(silence);
    for (i = 0; i < 4; i++)
        prev_sig[i] = signal(sigs[i], on_signal);
    hooked = 1;
}
#endif

/* ---- the emulation (not on DOS: there the chip or nothing) ----------------- */
#ifndef DGK_DOS

#define EG_OFF 0
#define EG_ATTACK 1
#define EG_DECAY 2
#define EG_SUSTAIN 3
#define EG_RELEASE 4
#define CLOCK 49716.0                /* the chip's sample rate: 14.31818 MHz / 288 */

typedef struct emu_op {
    double ph;                       /* the phase, cycles */
    float env;                       /* attenuation, chip units of 0.09375 dB (0 loud .. 1023 silent) */
    int stage;
    float out1, out2;                /* a modulator's last two outputs (its feedback) */
} emu_op;

static emu_op ops[22];               /* by register offset (00h-15h: 6, 7, 14, 15 unused) */
static uint32_t emu_rate = 22050, noise = 1;
static unsigned long emu_samples;    /* rendered so far (FW_OPL_LOG's times) */
static double lfo_t;
static float amp_table[1024];

static const float mult_table[16] = { 0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15 };
static const int mod_off[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

static int ch_fnum(int c)
{
    return reg[0xA0 + c] | (reg[0xB0 + c] & 3) << 8;
}

static int ch_block(int c)
{
    return (reg[0xB0 + c] >> 2) & 7;
}

/* An envelope rate (0-15) scaled by the key (KSR), as the chip's 0-63; seconds for the full 96 dB (decay and
 * release) or from silence (attack: about 14 times quicker). */
static double rate_seconds(int r, int o, int c, int attack)
{
    int rof = ch_block(c) * 2 + ((ch_fnum(c) >> 9) & 1), re;
    if (r == 0)
        return 0;
    if (!(reg[0x20 + o] & 0x10))
        rof >>= 2;
    re = 4 * r + rof;
    if (re > 63)
        re = 63;
    return (attack ? 2.826 : 39.28) / pow(2.0, (re - 4) / 4.0);
}

static void key(int o, int on)
{
    if (on) {
        ops[o].stage = EG_ATTACK;
        ops[o].ph = 0;
    } else if (ops[o].stage != EG_OFF)
        ops[o].stage = EG_RELEASE;
}

/* One operator's envelope a sample on (c: its channel, for the key scaling). */
static void envelope(int o, int c)
{
    emu_op *p = &ops[o];
    int ar = reg[0x60 + o] >> 4, dr = reg[0x60 + o] & 15, sl = reg[0x80 + o] >> 4, rr = reg[0x80 + o] & 15;
    float sl_units = sl == 15 ? 992.0f : sl * 32.0f;
    double t;
    switch (p->stage) {
    case EG_ATTACK:
        t = rate_seconds(ar, o, c, 1);
        if (ar == 15 || t < 1.0 / emu_rate)
            p->env = 0;
        else if (ar)
            p->env -= p->env * (float)(6.93 / (t * emu_rate)) + 0.02f;
        if (p->env <= 0.5f) {
            p->env = 0;
            p->stage = EG_DECAY;
        }
        break;
    case EG_DECAY:
        t = rate_seconds(dr, o, c, 0);
        if (dr)
            p->env += (float)(1023.0 / (t * emu_rate));
        if (p->env >= sl_units) {
            p->env = sl_units;
            p->stage = EG_SUSTAIN;
        }
        break;
    case EG_SUSTAIN:
        if (!(reg[0x20 + o] & 0x20)) {   /* not held: on down at the release rate */
            t = rate_seconds(rr, o, c, 0);
            if (rr)
                p->env += (float)(1023.0 / (t * emu_rate));
        }
        break;
    case EG_RELEASE:
        t = rate_seconds(rr, o, c, 0);
        if (rr)
            p->env += (float)(1023.0 / (t * emu_rate));
        break;
    default:
        p->env = 1023;
        break;
    }
    if (p->env >= 1023) {
        p->env = 1023;
        if (p->stage == EG_RELEASE || p->stage == EG_SUSTAIN)
            p->stage = EG_OFF;
    }
}

static float wave(double ph, int ws)
{
    double f = ph - floor(ph);
    float s = (float)sin(f * 2 * 3.14159265358979);
    switch (ws) {
    case 1:
        return f < 0.5 ? s : 0;
    case 2:
        return fabsf(s);
    case 3:
        return (f < 0.25 || (f >= 0.5 && f < 0.75)) ? fabsf(s) : 0;
    default:
        return s;
    }
}

/* An operator's level: its envelope, total level (0.75 dB steps), the tremolo if on; 0..1. */
static float level(int o, float trem)
{
    float att = ops[o].env + (reg[0x40 + o] & 63) * 8.0f + ((reg[0x20 + o] & 0x80) ? trem : 0);
    int a = (int)att;
    return a >= 1023 ? 0 : amp_table[a < 0 ? 0 : a];
}

static float op_out(int o, int c, double freq, double mod, float trem, float vib)
{
    emu_op *p = &ops[o];
    float out;
    int ws = (reg[1] & 0x20) ? reg[0xE0 + o] & 3 : 0;
    double f = freq * mult_table[reg[0x20 + o] & 15] * ((reg[0x20 + o] & 0x40) ? 1.0 + vib : 1.0);
    envelope(o, c);
    out = p->stage == EG_OFF ? 0 : wave(p->ph + mod, ws) * level(o, trem);
    p->ph += f / emu_rate;
    if (p->ph > 1e6)
        p->ph -= floor(p->ph);
    return out;
}

static float noise_bit(void)
{
    noise = (noise >> 1) ^ (-(int32_t)(noise & 1) & 0x800302u);   /* the chip's 23-bit noise */
    return (noise & 1) ? 1.0f : -1.0f;
}

static void emu_render(int32_t *acc, int frames, void *user)
{
    int f, c;
    DGK_UNUSED(user);
    for (f = 0; f < frames; f++) {
        double lfo = lfo_t;
        float trem = (float)((1.0 + sin(lfo * 2 * 3.14159265358979 * 3.7)) * 0.5 * ((reg[0xBD] & 0x80) ? 51.0 : 10.7));
        float vib = (float)(sin(lfo * 2 * 3.14159265358979 * 6.1) * ((reg[0xBD] & 0x40) ? 0.008 : 0.004));
        float sum = 0, nz = noise_bit();
        int rhythm = reg[0xBD] & 0x20;
        for (c = 0; c < 9; c++) {
            int m = mod_off[c], k = m + 3, fb = (reg[0xC0 + c] >> 1) & 7, cnt = reg[0xC0 + c] & 1;
            double freq = ch_fnum(c) * CLOCK / (double)(1 << (20 - ch_block(c)));
            float mo, co;
            if (rhythm && c == 7) {            /* the hi-hat (modulator) and snare (carrier): noise and a tone */
                float hh = op_out(m, c, freq, 0, trem, vib);
                float sd = op_out(k, c, freq, 0, trem, vib);
                sum += fabsf(hh) * nz * 0.8f + sd * 0.4f + fabsf(sd) * nz * 0.8f;
                continue;
            }
            if (rhythm && c == 8) {            /* the tom (modulator) and cymbal (carrier) */
                float tom = op_out(m, c, freq, 0, trem, vib);
                float cy = op_out(k, c, freq * 1.41, 0, trem, vib);
                sum += tom + fabsf(cy) * nz * 0.6f;
                continue;
            }
            mo = op_out(m, c, freq, fb ? (ops[m].out1 + ops[m].out2) * 0.5 * pow(2.0, fb - 7) * 2.0 : 0, trem, vib);
            ops[m].out2 = ops[m].out1;
            ops[m].out1 = mo;
            co = op_out(k, c, freq, cnt ? 0 : mo * 2.0, trem, vib);
            sum += (cnt ? mo + co : co) * ((rhythm && c == 6) ? 2.0f : 1.0f);
        }
        lfo_t += 1.0 / emu_rate;
        acc[f] += (int32_t)(sum * 8000.0f);
    }
    emu_samples += (unsigned long)frames;
}

#endif

/* ---- the interface ---------------------------------------------------------- */

static unsigned long wlog_us(void)
{
#ifdef DGK_DOS
    return (unsigned long)dgk_now_us();
#else
    return (unsigned long)(emu_samples * 1000000.0 / emu_rate);
#endif
}

int opl_open(void)
{
    int i;
    memset(reg, 0, sizeof reg);
    if (getenv("FW_OPL_LOG") && !wlog && (wlog = fopen(getenv("FW_OPL_LOG"), "w")) != NULL)
        setvbuf(wlog, NULL, _IOLBF, 4096);       /* (a line at a time: the program may end without closing it) */
    if (getenv("FW_OPL_LOG"))
        dgk_log("FW-OPL log %s: %s", getenv("FW_OPL_LOG"), wlog ? "open" : "cannot open");
#ifdef DGK_DOS
    kind = chip_detect();
    if (kind == OPL_CHIP3)
        write_gap = 5;
    if (kind != OPL_NONE)
        hook();
#else
    for (i = 0; i < 1024; i++)
        amp_table[i] = (float)pow(10.0, -i * 0.09375 / 20.0);
    for (i = 0; i < 22; i++) {
        ops[i].stage = EG_OFF;
        ops[i].env = 1023;
    }
    emu_rate = 22050;
    dgk_mix_stream(emu_render, NULL);
    kind = OPL_EMULATED;
#endif
    DGK_UNUSED(i);
    if (kind != OPL_NONE) {
        opl_reset();
        opl_write(1, 0x20);                  /* the waveforms */
    }
    return kind;
}

void opl_write(int r, int v)
{
    int old;
    if (kind == OPL_NONE || r < 0 || r > 255)
        return;
#ifdef DGK_DOS
    if (wlog)
        fprintf(wlog, "%lu %02x %02x\n", wlog_us(), r, v & 255);
    chip_write(r, v);
    reg[r] = (uint8_t)v;
#else
    dgk_mix_lock();
    if (wlog)
        fprintf(wlog, "%lu %02x %02x\n", wlog_us(), r, v & 255);
    old = reg[r];
    reg[r] = (uint8_t)v;
    if (r >= 0xB0 && r <= 0xB8 && ((old ^ v) & 0x20)) {   /* a channel keyed on or off */
        key(mod_off[r - 0xB0], v & 0x20);
        key(mod_off[r - 0xB0] + 3, v & 0x20);
    } else if (r == 0xBD && (v & 0x20)) {      /* the drums keyed: BD both of ch 6; SD ch 7's carrier, HH its
                                                  modulator; TOM ch 8's modulator, CYM its carrier */
        static const int bit[5] = { 0x10, 0x08, 0x04, 0x02, 0x01 };
        static const int opo[5][2] = { { 16, 19 }, { 20, -1 }, { 18, -1 }, { 21, -1 }, { 17, -1 } };
        int b;
        for (b = 0; b < 5; b++)
            if ((old ^ v) & bit[b]) {
                key(opo[b][0], v & bit[b]);
                if (opo[b][1] >= 0)
                    key(opo[b][1], v & bit[b]);
            }
    }
    dgk_mix_unlock();
#endif
    DGK_UNUSED(old);
}

void opl_reset(void)
{
    int r;
    if (kind == OPL_NONE)
        return;
    for (r = 0xB0; r <= 0xB8; r++)           /* keys off first */
        opl_write(r, 0);
    opl_write(0xBD, 0);
    for (r = 0x20; r <= 0xF5; r++)
        opl_write(r, (r >= 0x40 && r <= 0x55) ? 0x3F : 0);   /* (total levels at their quietest) */
}

void opl_close(void)
{
    if (kind == OPL_NONE)
        return;
    opl_reset();
    if (wlog) {
        fclose(wlog);
        wlog = NULL;
    }
#ifndef DGK_DOS
    dgk_mix_stream(NULL, NULL);
#endif
    kind = OPL_NONE;
}
