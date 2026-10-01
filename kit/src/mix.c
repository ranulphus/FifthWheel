/* mix.c - the integer mixer (see dgk/mix.h). */
#include "dgk/mix.h"
#include <string.h>

void plat_audio_lock(void);
void plat_audio_unlock(void);

#define RAMP 1024            /* volume change per output frame, 8.8 x 256: unity in 64 frames (3 ms) */

typedef struct voice {
    const dgk_sound *s;
    uint32_t pos, frac;      /* sample index and 16-bit fraction */
    uint32_t step;           /* 16.16 samples per output frame */
    int vol;                 /* the target: 256 = unity */
    int cur;                 /* where the volume is, x 256 (ramps to vol: no clicks) */
    int stopping;            /* fading out; the voice is free at silence */
    int flags;
} voice;

static voice voices[DGK_MIX_VOICES];
static uint32_t out_rate = 22050;

void dgk_mix_lock(void)
{
    plat_audio_lock();
}

void dgk_mix_unlock(void)
{
    plat_audio_unlock();
}

void dgk_mix_init(uint32_t rate)
{
    out_rate = rate;
    memset(voices, 0, sizeof voices);
}

static uint32_t step_for(const dgk_sound *s, uint32_t pitch_q16)
{
    return (uint32_t)(((uint64_t)pitch_q16 * s->rate) / out_rate);
}

int dgk_mix_play(const dgk_sound *s, int vol_q8, uint32_t pitch_q16, int flags)
{
    int i, v = -1;
    dgk_mix_lock();
    for (i = 0; i < DGK_MIX_VOICES; i++)
        if (!voices[i].s) {
            v = i;
            break;
        }
    if (v >= 0) {
        voices[v].s = s;
        voices[v].pos = voices[v].frac = 0;
        voices[v].step = step_for(s, pitch_q16);
        voices[v].vol = vol_q8;
        voices[v].cur = 0;
        voices[v].stopping = 0;
        voices[v].flags = flags;
    }
    dgk_mix_unlock();
    return v;
}

void dgk_mix_set(int v, int vol_q8, uint32_t pitch_q16)
{
    if (v < 0 || v >= DGK_MIX_VOICES)
        return;
    dgk_mix_lock();
    if (voices[v].s && !voices[v].stopping) {
        voices[v].vol = vol_q8;
        voices[v].step = step_for(voices[v].s, pitch_q16);
    }
    dgk_mix_unlock();
}

void dgk_mix_stop(int v)
{
    if (v < 0 || v >= DGK_MIX_VOICES)
        return;
    dgk_mix_lock();
    voices[v].vol = 0;                         /* fade out; render frees it */
    voices[v].stopping = 1;
    dgk_mix_unlock();
}

void dgk_mix_render(int16_t *out, int frames)
{
    static int32_t acc[1024];
    int i, f;
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        memset(acc, 0, (size_t)n * sizeof acc[0]);
        for (i = 0; i < DGK_MIX_VOICES; i++) {
            voice *v = &voices[i];
            if (!v->s)
                continue;
            for (f = 0; f < n; f++) {
                const int16_t *p = v->s->pcm;
                int32_t a = p[v->pos], b, x;
                uint32_t next = v->pos + 1;
                if (next >= v->s->frames)
                    next = (v->flags & DGK_MIX_LOOP) ? 0 : v->pos;
                b = p[next];
                x = a + (int32_t)(((int64_t)(b - a) * (int32_t)v->frac) >> 16);   /* linear */
                if (v->cur < v->vol << 8)
                    v->cur = v->cur + RAMP < v->vol << 8 ? v->cur + RAMP : v->vol << 8;
                else if (v->cur > v->vol << 8)
                    v->cur = v->cur - RAMP > v->vol << 8 ? v->cur - RAMP : v->vol << 8;
                acc[f] += (x * (v->cur >> 8)) >> 8;
                if (v->stopping && v->cur == 0) {
                    v->s = NULL;
                    break;
                }
                v->frac += v->step;
                v->pos += v->frac >> 16;
                v->frac &= 0xFFFF;
                if (v->pos >= v->s->frames) {
                    if (v->flags & DGK_MIX_LOOP)
                        v->pos %= v->s->frames;
                    else {
                        v->s = NULL;
                        break;
                    }
                }
            }
        }
        for (f = 0; f < n; f++)
            out[f] = (int16_t)(acc[f] > 32767 ? 32767 : acc[f] < -32768 ? -32768 : acc[f]);
        out += n;
        frames -= n;
    }
}
