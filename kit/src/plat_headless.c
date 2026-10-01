/* plat_headless.c - the platform for tests: an OSMesa context (Mesa's
 * software renderer, which DOS-GL's conformance tests use as the reference),
 * a virtual clock that advances one frame (1/60 s) per swap, no input and no
 * audio device. With DGK_WAV=FILE set, the mixer's output in step with the
 * virtual clock goes to FILE (16-bit mono WAV): the same samples every run. */
#include "plat.h"
#include "dgk/log.h"
#include "dgk/mix.h"
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

static OSMesaContext ctx;
static unsigned char *buffer;
static int w, h;
static uint64_t now_us;
static uint32_t audio_rate;
static FILE *wav;
static uint64_t wav_start_us;
static uint32_t wav_frames;

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void wav_header(void)
{
    unsigned char hd[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                             1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
    put32(hd + 4, 36 + wav_frames * 2);
    put32(hd + 24, audio_rate);
    put32(hd + 28, audio_rate * 2);
    put32(hd + 40, wav_frames * 2);
    fseek(wav, 0, SEEK_SET);
    fwrite(hd, 1, sizeof hd, wav);
    fseek(wav, 0, SEEK_END);
}

/* The mixer's output up to the virtual clock's time. */
static void wav_catch_up(void)
{
    int16_t buf[512];
    uint32_t due;
    if (!wav)
        return;
    due = (uint32_t)((now_us - wav_start_us) * audio_rate / 1000000);
    while (wav_frames < due) {
        int n = (int)DGK_MIN(due - wav_frames, (uint32_t)DGK_ARRAY_LEN(buf));
        dgk_mix_render(buf, n);
        fwrite(buf, 2, (size_t)n, wav);           /* little-endian hosts */
        wav_frames += (uint32_t)n;
    }
}

int plat_open(const plat_config *c, int *width, int *height)
{
    w = c->width;
    h = c->height;
    audio_rate = c->audio_rate;
    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    buffer = (unsigned char *)malloc((size_t)w * h * 4);
    if (!ctx || !buffer || !OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, w, h)) {
        dgk_log("FW-ERROR OSMesa %dx%d", w, h);
        return -1;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 0);
    *width = w;
    *height = h;
    return 0;
}

void plat_close(void)
{
    if (ctx)
        OSMesaDestroyContext(ctx);
    free(buffer);
    ctx = NULL;
    buffer = NULL;
}

void plat_pump(void)
{
}

int plat_next_event(plat_event *e)
{
    DGK_UNUSED(e);
    return 0;
}

void plat_swap(void)
{
    glFinish();
    now_us += 1000000 / 60;
    wav_catch_up();
}

uint64_t plat_now_us(void)
{
    return now_us;
}

int plat_snapshot(const char *name)
{
    char path[80];
    FILE *f;
    int x, y;
    glFinish();
    snprintf(path, sizeof path, "out/%s.ppm", name);
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++)                  /* OSMESA_Y_UP 0: row 0 is the top */
        for (x = 0; x < w; x++)
            fwrite(buffer + ((size_t)y * w + x) * 4, 3, 1, f);
    return fclose(f) == 0 ? 0 : -1;
}

void plat_audio_open(void)
{
    const char *path = getenv("DGK_WAV");
    if (!path || wav)
        return;
    wav = fopen(path, "wb");
    if (!wav) {
        dgk_log("FW-WARN cannot write %s", path);
        return;
    }
    wav_start_us = now_us;
    wav_frames = 0;
    wav_header();
}

void plat_audio_close(void)
{
    if (!wav)
        return;
    wav_catch_up();
    wav_header();
    fclose(wav);
    wav = NULL;
}

int plat_audio_underruns(int *underruns, int *chunks)
{
    *underruns = *chunks = 0;
    return 0;
}

void plat_audio_lock(void)
{
}

void plat_audio_unlock(void)
{
}

const char *plat_describe(void)
{
    return "OSMesa (headless)";
}
