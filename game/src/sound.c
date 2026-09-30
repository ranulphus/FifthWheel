/* sound.c - synthesised lorry sounds (see sound.h). */
#include "sound.h"
#include "dgk/mix.h"
#include <math.h>
#include <string.h>

#define RATE 22050
#define FW_PI 3.14159265358979

/* The engine at 1000 rpm: six cylinders firing 50 times a second; 10
 * periods of 441 samples, so the loop is seamless. */
#define ENGINE_FRAMES 4410
static int16_t engine_pcm[ENGINE_FRAMES];
static int16_t hiss_pcm[RATE / 2];
static int16_t beep_pcm[RATE * 6 / 10];
static int16_t puff_pcm[RATE / 8];
static dgk_sound engine, hiss, beep, puff;
static int engine_voice = -1, beep_voice = -1;
static uint32_t noise = 12345;

static int noise16(void)
{
    noise = noise * 1103515245u + 12345u;
    return (int)(noise >> 16) - 32768;
}

void sound_init(void)
{
    int i;
    for (i = 0; i < ENGINE_FRAMES; i++) {
        double t = (double)i / RATE, f = 50.0;
        double s = 0.55 * sin(2 * FW_PI * f * t) + 0.35 * sin(2 * FW_PI * 2 * f * t + 0.3) +
                   0.45 * sin(2 * FW_PI * 3 * f * t + 1.1) + 0.18 * sin(2 * FW_PI * 6 * f * t + 0.5);
        double firing = fmod(t * f, 1.0);                     /* a knock on each firing */
        s += 0.35 * exp(-firing * 12.0) * (noise16() / 32768.0);
        engine_pcm[i] = (int16_t)(s * 9000.0);
    }
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
    engine.pcm = engine_pcm; engine.frames = ENGINE_FRAMES; engine.rate = RATE;
    hiss.pcm = hiss_pcm; hiss.frames = DGK_ARRAY_LEN(hiss_pcm); hiss.rate = RATE;
    beep.pcm = beep_pcm; beep.frames = DGK_ARRAY_LEN(beep_pcm); beep.rate = RATE;
    puff.pcm = puff_pcm; puff.frames = DGK_ARRAY_LEN(puff_pcm); puff.rate = RATE;
    engine_voice = dgk_mix_play(&engine, 120, 0x10000 * 6 / 10, DGK_MIX_LOOP);
}

void sound_tick(const rig *r)
{
    uint32_t pitch = (uint32_t)(r->rpm / 1000.0f * 65536.0f);
    int vol = (int)(110 + 110 * r->throttle);
    dgk_mix_set(engine_voice, vol, pitch);
    if (r->air_release)
        dgk_mix_play(&hiss, 160, 0x10000, 0);
    if (r->shifted > 0)
        dgk_mix_play(&puff, 120, 0x10000, 0);
    if (r->gear < 0 && beep_voice < 0)
        beep_voice = dgk_mix_play(&beep, 90, 0x10000, DGK_MIX_LOOP);
    else if (r->gear > 0 && beep_voice >= 0) {
        dgk_mix_stop(beep_voice);
        beep_voice = -1;
    }
}

void sound_stop(void)
{
    dgk_mix_stop(engine_voice);
    dgk_mix_stop(beep_voice);
    engine_voice = beep_voice = -1;
}
