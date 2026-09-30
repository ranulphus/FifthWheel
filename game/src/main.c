/* Fifth Wheel - F0: the kit's first scene. A toy lorry (tractor and box
 * trailer) on a field, seen by the game's angled camera circling it, with
 * the HUD font and, with -tone, a 440 Hz tone through the mixer. It proves
 * the three builds (DOS, Linux, headless) and the harness before the game
 * proper starts (plan milestone F0).
 *
 *   FWHEEL [kit options, see dgk/app.h] [-tone]
 */
#include "dgk/dgk.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const dgk_font_data fw_font;

#define MAX_VERTS 2048
#define MAX_TRIS  2048

/* A mesh being built: flat-shaded boxes and quads, lighting baked in. */
typedef struct builder {
    float pos[MAX_VERTS * 3];
    uint8_t rgba[MAX_VERTS * 4];
    uint16_t idx[MAX_TRIS * 3];
    int nverts, ntris;
} builder;

/* Baked light: warm sun from the upper left (north-west), strong ambient. */
static float light(float nx, float ny, float nz)
{
    const float sx = -0.45f, sy = 0.75f, sz = -0.48f;         /* towards the sun */
    float d = nx * sx + ny * sy + nz * sz;
    return 0.58f + 0.42f * (d > 0 ? d : 0);
}

static void quad(builder *b, const float *p0, const float *p1, const float *p2, const float *p3, uint32_t rgb)
{
    float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    float vx = p3[0] - p0[0], vy = p3[1] - p0[1], vz = p3[2] - p0[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz), k = light(nx / l, ny / l, nz / l);
    const float *p[4];
    int i, base = b->nverts;
    if (b->nverts + 4 > MAX_VERTS || b->ntris + 2 > MAX_TRIS) {
        dgk_log("FW-ERROR mesh builder full (%d vertices, %d triangles)", b->nverts, b->ntris);
        return;
    }
    p[0] = p0; p[1] = p1; p[2] = p2; p[3] = p3;
    for (i = 0; i < 4; i++) {
        float r = ((rgb >> 16) & 255) * k, g = ((rgb >> 8) & 255) * k, bl = (rgb & 255) * k;
        memcpy(&b->pos[b->nverts * 3], p[i], 3 * sizeof(float));
        b->rgba[b->nverts * 4 + 0] = (uint8_t)(r > 255 ? 255 : r);
        b->rgba[b->nverts * 4 + 1] = (uint8_t)(g > 255 ? 255 : g);
        b->rgba[b->nverts * 4 + 2] = (uint8_t)(bl > 255 ? 255 : bl);
        b->rgba[b->nverts * 4 + 3] = 255;
        b->nverts++;
    }
    b->idx[b->ntris * 3 + 0] = (uint16_t)base;
    b->idx[b->ntris * 3 + 1] = (uint16_t)(base + 1);
    b->idx[b->ntris * 3 + 2] = (uint16_t)(base + 2);
    b->idx[b->ntris * 3 + 3] = (uint16_t)base;
    b->idx[b->ntris * 3 + 4] = (uint16_t)(base + 2);
    b->idx[b->ntris * 3 + 5] = (uint16_t)(base + 3);
    b->ntris += 2;
}

/* An axis-aligned box from (x0,y0,z0) to (x1,y1,z1): five faces (no bottom). */
static void box(builder *b, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb)
{
    float v[8][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
                      { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } };
    quad(b, v[7], v[6], v[5], v[4], rgb);        /* top */
    quad(b, v[3], v[2], v[6], v[7], rgb);        /* +z */
    quad(b, v[1], v[0], v[4], v[5], rgb);        /* -z */
    quad(b, v[2], v[1], v[5], v[6], rgb);        /* +x */
    quad(b, v[0], v[3], v[7], v[4], rgb);        /* -x */
}

typedef struct game {
    builder ground, lorry;
    dgk_mesh ground_mesh, lorry_mesh;
    dgk_font font;
    int16_t tone_pcm[22050 / 20];                /* 440 Hz: 50 ms, 22 whole cycles */
    dgk_sound tone;
    int tone_on;
    float yaw, yaw_prev;
    uint64_t fps_t0;
    uint32_t fps_frames;
    float fps;
} game;

static void build_ground(builder *b)
{
    int x, z;
    for (z = -8; z < 8; z++)
        for (x = -8; x < 8; x++) {
            float p[4][3] = { { x * 4.0f, 0, z * 4.0f }, { x * 4.0f + 4, 0, z * 4.0f },
                              { x * 4.0f + 4, 0, z * 4.0f + 4 }, { x * 4.0f, 0, z * 4.0f + 4 } };
            quad(b, p[3], p[2], p[1], p[0], ((x + z) & 1) ? 0x5DBB3Fu : 0x4FA535u);
        }
    {   /* a bold road across the field, its markings a hair above it */
        float r[4][3] = { { -32, 0.02f, -3 }, { 32, 0.02f, -3 }, { 32, 0.02f, 3 }, { -32, 0.02f, 3 } };
        quad(b, r[3], r[2], r[1], r[0], 0x5A5F6Au);
        for (x = -32; x < 32; x += 4) {
            float m[4][3] = { { x + 0.5f, 0.04f, -0.2f }, { x + 2.5f, 0.04f, -0.2f },
                              { x + 2.5f, 0.04f, 0.2f }, { x + 0.5f, 0.04f, 0.2f } };
            quad(b, m[3], m[2], m[1], m[0], 0xFFF4D0u);
        }
    }
}

/* Toy proportions: a stubby cab, big wheels, a tall box trailer. */
static void build_lorry(builder *b)
{
    box(b, 2.2f, 0.8f, -1.2f, 5.0f, 3.4f, 1.2f, 0xE8402Au);   /* cab */
    box(b, 4.4f, 1.8f, -1.0f, 5.2f, 2.9f, 1.0f, 0x9FD8FFu);   /* windscreen */
    box(b, 2.6f, 3.4f, 0.6f, 2.9f, 4.3f, 0.9f, 0xC8C8D0u);    /* exhaust stack */
    box(b, 1.4f, 0.7f, -1.1f, 2.4f, 1.1f, 1.1f, 0x3A3A44u);   /* chassis */
    box(b, -7.0f, 1.0f, -1.3f, 2.0f, 4.2f, 1.3f, 0xFFD23Fu);  /* box trailer */
    box(b, -6.9f, 4.2f, -1.2f, 1.9f, 4.35f, 1.2f, 0xFFFFFFu); /* its roof trim */
    {   /* wheels: 1.3x scale, dark boxes with bright hubs */
        static const float wx[4] = { 4.3f, 1.6f, -4.8f, -6.1f };
        int i, s;
        for (i = 0; i < 4; i++)
            for (s = -1; s <= 1; s += 2) {
                float z0 = s < 0 ? -1.55f : 1.05f;
                box(b, wx[i] - 0.65f, 0.0f, z0, wx[i] + 0.65f, 1.3f, z0 + 0.5f, 0x26262Cu);
                box(b, wx[i] - 0.25f, 0.45f, s < 0 ? z0 - 0.05f : z0 + 0.5f, wx[i] + 0.25f, 0.85f,
                    s < 0 ? z0 : z0 + 0.55f, 0xD0D4DCu);
            }
    }
}

static void mesh_of(dgk_mesh *m, const builder *b)
{
    m->pos = b->pos;
    m->rgba = b->rgba;
    m->idx = b->idx;
    m->nverts = b->nverts;
    m->ntris = b->ntris;
}

static int init(void *u)
{
    game *g = (game *)u;
    int i, argc_tone = g->tone_on;
    build_ground(&g->ground);
    build_lorry(&g->lorry);
    mesh_of(&g->ground_mesh, &g->ground);
    mesh_of(&g->lorry_mesh, &g->lorry);
    if (dgk_font_load(&g->font, &fw_font) != 0)
        return -1;
    for (i = 0; i < (int)DGK_ARRAY_LEN(g->tone_pcm); i++)
        g->tone_pcm[i] = (int16_t)(9000.0 * sin(2.0 * 3.14159265358979 * 440.0 * i / 22050.0));
    g->tone.pcm = g->tone_pcm;
    g->tone.frames = DGK_ARRAY_LEN(g->tone_pcm);
    g->tone.rate = 22050;
    if (argc_tone)
        dgk_mix_play(&g->tone, 256, 0x10000, DGK_MIX_LOOP);
    glClearColor(0.55f, 0.78f, 0.95f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    dgk_log("FW-SCENE ground %d tris, lorry %d tris", g->ground.ntris, g->lorry.ntris);
    g->fps_t0 = dgk_now_us();
    return 0;
}

static void tick(void *u)
{
    game *g = (game *)u;
    g->yaw_prev = g->yaw;
    g->yaw += 0.4f * 3.14159265f / 180.0f;
    if (dgk_app.key_pressed[DGK_KEY_ESCAPE])
        dgk_app.quit = 1;
}

static void draw(void *u, float alpha)
{
    game *g = (game *)u;
    float yaw = g->yaw_prev + (g->yaw - g->yaw_prev) * alpha;
    const float dist = 20.0f, pitch = 55.0f * 3.14159265f / 180.0f;
    dgk_v3 at = { -1.0f, 1.5f, 0 }, up = { 0, 1, 0 }, eye;
    char line[64];
    uint64_t now;

    eye.x = at.x + dist * cosf(pitch) * cosf(yaw);
    eye.y = at.y + dist * sinf(pitch);
    eye.z = at.z + dist * cosf(pitch) * sinf(yaw);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    dgk_gfx_perspective(50.0f, (float)dgk_app.width / dgk_app.height, 2.0f, 80.0f);
    dgk_gfx_look_at(eye, at, up);
    dgk_gfx_draw_mesh(&g->ground_mesh);
    dgk_gfx_draw_mesh(&g->lorry_mesh);

    dgk_gfx_overlay_begin();
    dgk_text(&g->font, 20, 16, 2.0f, 0xFFD23FFFu, "FIFTH WHEEL");
    dgk_text(&g->font, 22, 60, 1.0f, 0xFFFFFFFFu, "JUGGERNAUT 3D FOR DOS-GL");
    if (dgk_app.fixed)
        snprintf(line, sizeof line, "F0 KIT CHECK  FRAME %lu", (unsigned long)dgk_app.frame);
    else {
        now = dgk_now_us();
        if (++g->fps_frames >= 30 || now - g->fps_t0 > 1000000) {
            g->fps = g->fps_frames * 1e6f / (float)(now - g->fps_t0 ? now - g->fps_t0 : 1);
            g->fps_frames = 0;
            g->fps_t0 = now;
        }
        snprintf(line, sizeof line, "F0 KIT CHECK  %.1f FPS", g->fps);
    }
    dgk_text(&g->font, 20, 446, 1.0f, 0x9FFFB0FFu, line);
    dgk_gfx_overlay_end();
}

int main(int argc, char **argv)
{
    static game g;
    static const dgk_app_desc desc = { "Fifth Wheel", init, tick, draw, NULL };
    int i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-tone"))
            g.tone_on = 1;
    return dgk_app_run(&desc, &g, argc, argv);
}
