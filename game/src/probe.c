/* probe.c - the performance probe (see probe.h). */
#include "probe.h"
#include "dgk/dgk.h"
#include "dgk/bench.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GRID 64                                  /* up to 64 x 64 cells: 8192 triangles */
#define MAX_TRIS (GRID * GRID * 2)
enum { PATH_ARRAYS, PATH_LIST, PATH_IMMEDIATE };
static const char *path_name[3] = { "arr", "list", "imm" };

typedef struct probe_case {
    int tris, draws, path, textured;
} probe_case;

static const probe_case cases[] = {
    { 1000, 1, PATH_ARRAYS, 0 }, { 2000, 1, PATH_ARRAYS, 0 }, { 4000, 1, PATH_ARRAYS, 0 }, { 8000, 1, PATH_ARRAYS, 0 },
    { 1000, 1, PATH_LIST, 0 },   { 2000, 1, PATH_LIST, 0 },   { 4000, 1, PATH_LIST, 0 },   { 8000, 1, PATH_LIST, 0 },
    { 1000, 1, PATH_IMMEDIATE, 0 }, { 2000, 1, PATH_IMMEDIATE, 0 }, { 4000, 1, PATH_IMMEDIATE, 0 },
    { 1000, 1, PATH_ARRAYS, 1 }, { 2000, 1, PATH_ARRAYS, 1 }, { 4000, 1, PATH_ARRAYS, 1 },
    { 2000, 50, PATH_ARRAYS, 0 }, { 2000, 100, PATH_ARRAYS, 0 }, { 2000, 200, PATH_ARRAYS, 0 },
    { 2000, 400, PATH_ARRAYS, 0 },
};
#define NCASES ((int)(sizeof cases / sizeof cases[0]))

static float pos[(GRID + 1) * (GRID + 1) * 3], st[(GRID + 1) * (GRID + 1) * 2];
static uint8_t rgba[(GRID + 1) * (GRID + 1) * 4];
static uint16_t idx[MAX_TRIS * 3];
static GLuint texture, list;
static int current = -1, frame_in_case, frames_per_case, warmup;

/* A (GRID+1)^2 vertex ground grid, 1.25 m cells, under the game's camera:
 * triangles of the in-game sizes (roughly 100-600 pixels at 640x480). */
static void build(void)
{
    int x, z, n = 0, k = 0;
    for (z = 0; z <= GRID; z++)
        for (x = 0; x <= GRID; x++) {
            pos[k * 3 + 0] = (x - GRID / 2) * 1.25f;
            pos[k * 3 + 1] = 0.3f * sinf(x * 0.4f) * cosf(z * 0.3f);
            pos[k * 3 + 2] = (z - GRID / 2) * 1.25f;
            st[k * 2 + 0] = x * 0.25f;
            st[k * 2 + 1] = z * 0.25f;
            rgba[k * 4 + 0] = (uint8_t)(80 + (x * 7) % 90);
            rgba[k * 4 + 1] = (uint8_t)(150 + (z * 5) % 80);
            rgba[k * 4 + 2] = 60;
            rgba[k * 4 + 3] = 255;
            k++;
        }
    for (z = 0; z < GRID; z++)
        for (x = 0; x < GRID; x++) {
            uint16_t a = (uint16_t)(z * (GRID + 1) + x), b = (uint16_t)(a + 1), c = (uint16_t)(a + GRID + 1),
                     d = (uint16_t)(c + 1);
            idx[n++] = a; idx[n++] = c; idx[n++] = b;
            idx[n++] = b; idx[n++] = c; idx[n++] = d;
        }
}

static void submit(const probe_case *c)
{
    int per = c->tris / c->draws, i;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, rgba);
    if (c->textured) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, st);
    }
    for (i = 0; i < c->draws; i++)
        glDrawElements(GL_TRIANGLES, per * 3, GL_UNSIGNED_SHORT, idx + i * per * 3);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void immediate(const probe_case *c)
{
    int i;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < c->tris * 3; i++) {
        int v = idx[i];
        glColor4ubv(&rgba[v * 4]);
        glVertex3fv(&pos[v * 3]);
    }
    glEnd();
}

static void start_case(int i)
{
    const probe_case *c = &cases[i];
    char name[32];
    current = i;
    frame_in_case = 0;
    if (list) {
        glDeleteLists(list, 1);
        list = 0;
    }
    if (c->path == PATH_LIST) {
        list = glGenLists(1);
        glNewList(list, GL_COMPILE);
        submit(c);
        glEndList();
    }
    if (c->draws > 1)                                  /* FWP-2000-d100: 2000 triangles in 100 draws */
        snprintf(name, sizeof name, "FWP-%d-d%d", c->tris, c->draws);
    else                                               /* FWP-4000-list, FWP-1000-arr-tex */
        snprintf(name, sizeof name, "FWP-%d-%s%s", c->tris, path_name[c->path], c->textured ? "-tex" : "");
    dgk_app_bench_start(name, warmup);
}

int probe_init(int quick)
{
    static uint8_t tex[32 * 32 * 3];
    int i;
    frames_per_case = quick ? 20 : 120;
    warmup = quick ? 4 : 10;
    build();
    for (i = 0; i < 32 * 32; i++) {
        int on = ((i & 31) / 4 + (i / 32) / 4) & 1;
        tex[i * 3 + 0] = (uint8_t)(on ? 240 : 90);
        tex[i * 3 + 1] = (uint8_t)(on ? 220 : 110);
        tex[i * 3 + 2] = (uint8_t)(on ? 120 : 70);
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 32, 32, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    dgk_bench_run("FWHEEL", "F2");
    start_case(0);
    return 0;
}

void probe_draw(void)
{
    const probe_case *c = &cases[current];
    dgk_v3 eye = { 0, 34, 34 }, at = { 0, 0, -6 }, up = { 0, 1, 0 };
    glClearColor(0.55f, 0.78f, 0.95f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    dgk_gfx_perspective(42.0f, (float)dgk_app.width / dgk_app.height, 8.0f, 140.0f);
    dgk_gfx_look_at(eye, at, up);
    glRotatef(frame_in_case * 0.3f, 0, 1, 0);          /* not a still frame: the chip redraws everything */
    if (c->textured)
        glEnable(GL_TEXTURE_2D);
    if (c->path == PATH_LIST)
        glCallList(list);
    else if (c->path == PATH_IMMEDIATE)
        immediate(c);
    else
        submit(c);
    glDisable(GL_TEXTURE_2D);
    dgk_gfx_tris += (uint32_t)c->tris;
    if (++frame_in_case >= frames_per_case + warmup) {
        dgk_app_bench_stop("ok", "probe=1");
        if (current + 1 < NCASES)
            start_case(current + 1);
        else {
            char notes[32];
            snprintf(notes, sizeof notes, "probe=summary cases=%d", NCASES);
            dgk_bench_begin("FWP", 0);                     /* the run as a whole, for DOSBench's test list */
            dgk_bench_end("ok", notes);
            dgk_app.quit = 1;
        }
    }
}
