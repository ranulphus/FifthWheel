/* plat_headless.c - the platform for tests: an OSMesa context (Mesa's
 * software renderer, which DOS-GL's conformance tests use as the reference),
 * a virtual clock that advances one frame (1/60 s) per swap, no input and no
 * audio device (tests render the mixer themselves). */
#include "plat.h"
#include "dgk/log.h"
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

static OSMesaContext ctx;
static unsigned char *buffer;
static int w, h;
static uint64_t now_us;

int plat_open(const plat_config *c, int *width, int *height)
{
    w = c->width;
    h = c->height;
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
