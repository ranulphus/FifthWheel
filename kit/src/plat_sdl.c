/* plat_sdl.c - the platform on SDL3: DOS (the DOS-GL bridge, DOSGL's
 * tools/sdl/patches) and Linux (desktop OpenGL). */
#include "plat.h"
#include "dgk/log.h"
#include "dgk/mix.h"
#include <SDL3/SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef DGK_DOS
#include <GL/dosgl.h>
#endif

static SDL_Window *window;
static SDL_GLContext context;
static SDL_AudioStream *audio;
static char describe[160];

/* SDL asks for more samples: mix them (the mixer's lock is the stream's). */
static void SDLCALL audio_more(void *u, SDL_AudioStream *s, int additional, int total)
{
    static int16_t buf[1024];
    DGK_UNUSED(u);
    DGK_UNUSED(total);
    while (additional > 0) {
        int frames = DGK_MIN(additional / 2, (int)DGK_ARRAY_LEN(buf));
        if (frames <= 0)
            break;
        dgk_mix_render(buf, frames);
        SDL_PutAudioStreamData(s, buf, frames * 2);
        additional -= frames * 2;
    }
}

int plat_open(const plat_config *c, int *width, int *height)
{
    SDL_InitFlags flags = SDL_INIT_VIDEO | SDL_INIT_JOYSTICK;
    if (c->sound)
        flags |= SDL_INIT_AUDIO;
    if (!SDL_Init(flags)) {
        /* No Sound Blaster (BLASTER unset) fails SDL_Init: carry on silent. */
        if (!(c->sound && SDL_Init(flags & ~SDL_INIT_AUDIO))) {
            dgk_log("FW-ERROR SDL_Init: %s", SDL_GetError());
            return -1;
        }
        dgk_log("FW-WARN no audio: %s", SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow(c->title, c->width, c->height, SDL_WINDOW_OPENGL);
    if (!window) {
        dgk_log("FW-ERROR window %dx%d: %s", c->width, c->height, SDL_GetError());
        return -1;
    }
    context = SDL_GL_CreateContext(window);
    if (!context) {
        dgk_log("FW-ERROR OpenGL: %s", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(c->vsync ? 1 : 0);
    SDL_GetWindowSizeInPixels(window, width, height);
    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, (int)c->audio_rate };
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_more, NULL);
        if (audio)
            SDL_ResumeAudioStreamDevice(audio);
        else
            dgk_log("FW-WARN audio device: %s", SDL_GetError());
    }
    snprintf(describe, sizeof describe, "%s; video %s, audio %s", (const char *)glGetString(GL_RENDERER),
             SDL_GetCurrentVideoDriver(), audio ? SDL_GetCurrentAudioDriver() : "none");
    return 0;
}

void plat_close(void)
{
    if (audio)
        SDL_DestroyAudioStream(audio);
    audio = NULL;
    if (context)
        SDL_GL_DestroyContext(context);
    context = NULL;
    if (window)
        SDL_DestroyWindow(window);
    window = NULL;
    SDL_Quit();
}

void plat_pump(void)
{
    SDL_PumpEvents();
}

int plat_next_event(plat_event *e)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            e->type = PLAT_EV_QUIT;
            return 1;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.repeat)
                continue;
            e->type = PLAT_EV_KEY_DOWN;
            e->key = (int)ev.key.scancode;
            return 1;
        case SDL_EVENT_KEY_UP:
            e->type = PLAT_EV_KEY_UP;
            e->key = (int)ev.key.scancode;
            return 1;
        default:
            continue;
        }
    }
    return 0;
}

void plat_swap(void)
{
    SDL_GL_SwapWindow(window);
}

uint64_t plat_now_us(void)
{
    return SDL_GetTicksNS() / 1000;
}

int plat_snapshot(const char *name)
{
    char path[80];
#ifdef DGK_DOS
    snprintf(path, sizeof path, "C:\\OUT\\%s.PPM", name);
    return dglSnapshot(path);
#else
    int w = 0, h = 0, y;
    unsigned char *px;
    FILE *f;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    snprintf(path, sizeof path, "out/%s.ppm", name);
    px = (unsigned char *)malloc((size_t)w * h * 3);
    f = fopen(path, "wb");
    if (!px || !f) {
        free(px);
        if (f)
            fclose(f);
        return -1;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = h - 1; y >= 0; y--)
        fwrite(px + (size_t)y * w * 3, 3, (size_t)w, f);
    free(px);
    return fclose(f) == 0 ? 0 : -1;
#endif
}

void plat_audio_lock(void)
{
    if (audio)
        SDL_LockAudioStream(audio);
}

void plat_audio_unlock(void)
{
    if (audio)
        SDL_UnlockAudioStream(audio);
}

const char *plat_describe(void)
{
    return describe;
}
