/* sound.c - synthesised lorry sounds (see sound.h). */
#include "sound.h"
#include "dgk/log.h"
#include "dgk/mix.h"
#include <math.h>
#include <string.h>

#define RATE 22050
#define FW_PI 3.14159265358979

/* The engine at 1000 rpm: six cylinders firing 50 times a second; 10
 * periods of 441 samples, so the loops are seamless. */
#define ENGINE_FRAMES 4410
/* The horn: two tones whose periods are whole samples (67 and 53: 329 and
 * 416 Hz, a major third), looped over their least common multiple. */
#define HORN_A 67
#define HORN_B 53
#define HORN_FRAMES (HORN_A * HORN_B)

static int16_t engine_lo_pcm[ENGINE_FRAMES], engine_hi_pcm[ENGINE_FRAMES];
static int16_t hiss_pcm[RATE / 2];
static int16_t beep_pcm[RATE * 6 / 10];
static int16_t puff_pcm[RATE / 8];
static int16_t clunk_pcm[RATE / 5];
static int16_t chime_pcm[RATE * 7 / 10];
static int16_t horn_pcm[HORN_FRAMES];
static int16_t thud_pcm[RATE / 4];
static int16_t screech_pcm[RATE * 7 / 10];
static int16_t ref_pcm[RATE / 2];
static dgk_sound engine_lo, engine_hi, hiss, beep, puff, clunk, chime, horn, thud, screech, ref;
static int lo_voice = -1, hi_voice = -1, beep_voice = -1, horn_voice = -1;
static int hits_seen, jackknifes_seen, thud_wait;
static uint32_t noise = 12345;

static int noise16(void)
{
    noise = noise * 1103515245u + 12345u;
    return (int)(noise >> 16) - 32768;
}

static void sound_of(dgk_sound *s, const int16_t *pcm, uint32_t frames)
{
    s->pcm = pcm;
    s->frames = frames;
    s->rate = RATE;
}

static void make_engines(void)
{
    int i;
    for (i = 0; i < ENGINE_FRAMES; i++) {
        double t = (double)i / RATE, f = 50.0, firing = fmod(t * f, 1.0), lo, hi;
        /* Low: the fundamental and its first harmonics, a knock on each firing. */
        lo = 0.60 * sin(2 * FW_PI * f * t) + 0.40 * sin(2 * FW_PI * 2 * f * t + 0.3) +
             0.45 * sin(2 * FW_PI * 3 * f * t + 1.1) + 0.12 * sin(2 * FW_PI * 6 * f * t + 0.5);
        lo += 0.28 * exp(-firing * 12.0) * (noise16() / 32768.0);
        /* High: the odd harmonics up to the 13th (a rasp), breathy noise and a
         * turbo whistle at 52 x the firing rate (2600 Hz at 1000 rpm). */
        hi = 0.35 * sin(2 * FW_PI * f * t) + 0.30 * sin(2 * FW_PI * 3 * f * t + 0.7) +
             0.26 * sin(2 * FW_PI * 5 * f * t + 1.9) + 0.20 * sin(2 * FW_PI * 7 * f * t + 0.2) +
             0.14 * sin(2 * FW_PI * 9 * f * t + 2.4) + 0.10 * sin(2 * FW_PI * 13 * f * t + 0.9) +
             0.05 * sin(2 * FW_PI * 52 * f * t);
        hi += 0.11 * (0.5 + 0.5 * exp(-firing * 6.0)) * (noise16() / 32768.0);
        engine_lo_pcm[i] = (int16_t)(lo * 9000.0);
        engine_hi_pcm[i] = (int16_t)(hi * 9000.0);
    }
}

void sound_init(void)
{
    int i;
    make_engines();
    {
        int32_t lp = 0;                                       /* hiss: noise, a little brighter, dying away */
        for (i = 0; i < (int)DGK_ARRAY_LEN(hiss_pcm); i++) {
            int32_t n = noise16();
            double env = exp(-(double)i / (RATE * 0.12));
            lp += (n - lp) / 3;
            hiss_pcm[i] = (int16_t)((n - lp) * 0.8 * env);
        }
    }
    for (i = 0; i < (int)DGK_ARRAY_LEN(beep_pcm); i++)       /* 1 kHz for 0.3 s, then 0.3 s quiet */
        beep_pcm[i] = (int16_t)(i < RATE * 3 / 10 ? ((i / 11) & 1 ? 6000 : -6000) : 0);
    for (i = 0; i < (int)DGK_ARRAY_LEN(puff_pcm); i++)
        puff_pcm[i] = (int16_t)(noise16() * 0.35 * exp(-(double)i / (RATE * 0.03)));
    for (i = 0; i < (int)DGK_ARRAY_LEN(clunk_pcm); i++) {  /* a thump and a metallic knock */
        double t = (double)i / RATE;
        clunk_pcm[i] = (int16_t)((sin(2 * FW_PI * 70 * t) * 0.8 + sin(2 * FW_PI * 410 * t) * 0.3 +
                                  noise16() / 32768.0 * 0.25 * exp(-t * 60)) * exp(-t * 18) * 14000.0);
    }
    for (i = 0; i < (int)DGK_ARRAY_LEN(chime_pcm); i++) {  /* two bell notes, a fifth apart */
        double t = (double)i / RATE, t2 = t - 0.14;
        double s = sin(2 * FW_PI * 784 * t) * exp(-t * 6) + 0.4 * sin(2 * FW_PI * 1568 * t) * exp(-t * 9);
        if (t2 > 0)
            s += sin(2 * FW_PI * 1175 * t2) * exp(-t2 * 5) + 0.4 * sin(2 * FW_PI * 2350 * t2) * exp(-t2 * 8);
        chime_pcm[i] = (int16_t)(s * 7000.0);
    }
    for (i = 0; i < HORN_FRAMES; i++) {                    /* two buzzy tones: odd harmonics, softened */
        double a = 2 * FW_PI * (i % HORN_A) / HORN_A, b = 2 * FW_PI * (i % HORN_B) / HORN_B;
        double s = sin(a) + sin(3 * a) / 3 + sin(5 * a) / 6 + sin(b) + sin(3 * b) / 3 + sin(5 * b) / 6;
        horn_pcm[i] = (int16_t)(s * 4200.0);
    }
    {
        int32_t lp = 0;                                       /* thud: a low thump in muffled noise */
        for (i = 0; i < (int)DGK_ARRAY_LEN(thud_pcm); i++) {
            double t = (double)i / RATE;
            lp += (noise16() - lp) / 12;
            thud_pcm[i] = (int16_t)((sin(2 * FW_PI * 55 * t) * 0.9 + lp / 32768.0 * 1.6) * exp(-t * 14) * 15000.0);
        }
    }
    for (i = 0; i < (int)DGK_ARRAY_LEN(screech_pcm); i++) {   /* screech: a tyre's squeal, wobbling */
        double t = (double)i / RATE, env = (t < 0.04 ? t / 0.04 : 1.0) * exp(-t * 2.8);
        double wob = 0.06 * sin(2 * FW_PI * 13 * t), ph = 2 * FW_PI * t;
        double sq = sin(ph * 1900 * (1 + wob)) + 0.8 * sin(ph * 2150 * (1 - wob)) + 0.6 * sin(ph * 2420 * (1 + 0.5 * wob));
        screech_pcm[i] = (int16_t)((sq * 0.33 + 0.15 * noise16() / 32768.0) * env * 12000.0);
    }
    for (i = 0; i < (int)DGK_ARRAY_LEN(ref_pcm); i++)        /* the sound test's reference: 440 Hz */
        ref_pcm[i] = (int16_t)(sin(2 * FW_PI * 440 * i / RATE) * 9000.0);
    sound_of(&engine_lo, engine_lo_pcm, ENGINE_FRAMES);
    sound_of(&engine_hi, engine_hi_pcm, ENGINE_FRAMES);
    sound_of(&hiss, hiss_pcm, DGK_ARRAY_LEN(hiss_pcm));
    sound_of(&beep, beep_pcm, DGK_ARRAY_LEN(beep_pcm));
    sound_of(&puff, puff_pcm, DGK_ARRAY_LEN(puff_pcm));
    sound_of(&clunk, clunk_pcm, DGK_ARRAY_LEN(clunk_pcm));
    sound_of(&chime, chime_pcm, DGK_ARRAY_LEN(chime_pcm));
    sound_of(&horn, horn_pcm, HORN_FRAMES);
    sound_of(&thud, thud_pcm, DGK_ARRAY_LEN(thud_pcm));
    sound_of(&screech, screech_pcm, DGK_ARRAY_LEN(screech_pcm));
    sound_of(&ref, ref_pcm, DGK_ARRAY_LEN(ref_pcm));
}

/* The engine: both loops pitched by rpm, crossfaded from low to high
 * between 900 and 1900 rpm, louder with the throttle. */
static void engine(float rpm, float throttle)
{
    uint32_t pitch = (uint32_t)(rpm / 1000.0f * 65536.0f);
    float hi = DGK_CLAMP((rpm - 900.0f) / 1000.0f, 0.0f, 1.0f);
    int vol = (int)(105 + 115 * throttle);
    if (lo_voice < 0)
        lo_voice = dgk_mix_play(&engine_lo, 0, pitch, DGK_MIX_LOOP);
    if (hi_voice < 0)
        hi_voice = dgk_mix_play(&engine_hi, 0, pitch, DGK_MIX_LOOP);
    dgk_mix_set(lo_voice, (int)(vol * (1.0f - 0.7f * hi)), pitch);
    dgk_mix_set(hi_voice, (int)(vol * hi), pitch);
}

static void horn_held(int held)
{
    if (held && horn_voice < 0)
        horn_voice = dgk_mix_play(&horn, 150, 0x10000, DGK_MIX_LOOP);
    else if (!held && horn_voice >= 0) {
        dgk_mix_stop(horn_voice);
        horn_voice = -1;
    }
}

void sound_tick(const rig *r, int horn_down)
{
    engine(r->rpm, r->throttle);
    horn_held(horn_down);
    if (r->air_release)
        dgk_mix_play(&hiss, 160, 0x10000, 0);
    if (r->shifted > 0)
        dgk_mix_play(&puff, 120, 0x10000, 0);
    if (thud_wait > 0)
        thud_wait--;
    if (r->hits != hits_seen) {                      /* a bump; scraping along a wall thuds every 0.3 s */
        hits_seen = r->hits;
        if (!thud_wait) {
            dgk_mix_play(&thud, 230, 0x10000, 0);
            thud_wait = 18;
        }
    }
    if (r->jackknifes != jackknifes_seen) {
        jackknifes_seen = r->jackknifes;
        dgk_mix_play(&screech, 200, 0x10000, 0);
    }
    if (r->gear < 0 && beep_voice < 0)
        beep_voice = dgk_mix_play(&beep, 90, 0x10000, DGK_MIX_LOOP);
    else if (r->gear > 0 && beep_voice >= 0) {
        dgk_mix_stop(beep_voice);
        beep_voice = -1;
    }
}

void sound_clunk(void)
{
    dgk_mix_play(&clunk, 200, 0x10000, 0);
}

void sound_chime(int grade)
{
    static const uint32_t pitch[] = { 0x10000, 0xE000, 0x10000, 0x11F00, 0x14000 };   /* a step up a grade */
    dgk_mix_play(&chime, 170, pitch[DGK_CLAMP(grade, 0, 4)], 0);
}

void sound_stop(void)
{
    dgk_mix_stop(lo_voice);
    dgk_mix_stop(hi_voice);
    dgk_mix_stop(beep_voice);
    dgk_mix_stop(horn_voice);
    lo_voice = hi_voice = beep_voice = horn_voice = -1;
}

int sound_test_tick(uint32_t tick)
{
    static const struct { uint32_t at; const char *what; } steps[] = {
        { 0, "reference 440 Hz" }, { 42, "horn" }, { 90, "beeper" }, { 138, "chime" }, { 192, "clunk" },
        { 216, "thud" }, { 240, "screech" }, { 288, "engine" }, { 420, "end" }
    };
    int i;
    for (i = 0; i < (int)DGK_ARRAY_LEN(steps); i++)
        if (steps[i].at == tick)
            dgk_log("FW-SOUNDTEST %s at tick %lu", steps[i].what, (unsigned long)tick);
    switch (tick) {
    case 0:
        dgk_mix_play(&ref, 200, 0x10000, 0);
        break;
    case 42:
        horn_held(1);
        break;
    case 78:
        horn_held(0);
        break;
    case 90:
        beep_voice = dgk_mix_play(&beep, 140, 0x10000, DGK_MIX_LOOP);
        break;
    case 126:
        dgk_mix_stop(beep_voice);
        beep_voice = -1;
        break;
    case 138:
        sound_chime(4);
        break;
    case 192:
        sound_clunk();
        break;
    case 216:
        dgk_mix_play(&thud, 230, 0x10000, 0);
        break;
    case 240:
        dgk_mix_play(&screech, 200, 0x10000, 0);
        break;
    default:
        break;
    }
    if (tick >= 288 && tick < 408)                   /* idle to the red line over 2 s, full throttle */
        engine(600.0f + (tick - 288) * (1600.0f / 120.0f), 1.0f);
    else if (tick == 408)
        sound_stop();
    return tick < 420;
}
