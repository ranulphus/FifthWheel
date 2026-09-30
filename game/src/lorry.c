/* lorry.c - the toy lorry: a stubby cab, big wheels, a tall box trailer. */
#include "lorry.h"
#include <GL/gl.h>
#include <math.h>

#define TV 512
#define TT 512
static float tp[TV * 3], rp[TV * 3];
static uint8_t tc[TV * 4], rc[TV * 4];
static uint16_t ti[TT * 3], ri[TT * 3];

/* A wheel: a dark tyre block with a bright hub, x along the lorry. */
static void wheel(builder *b, float x, float side)
{
    float z0 = side < 0 ? -1.55f : 1.05f;
    mb_box(b, x - 0.65f, 0.0f, z0, x + 0.65f, 1.3f, z0 + 0.5f, 0x26262Cu);
    mb_box(b, x - 0.25f, 0.45f, side < 0 ? z0 - 0.05f : z0 + 0.5f, x + 0.25f, 0.85f, side < 0 ? z0 : z0 + 0.55f,
           0xD0D4DCu);
}

void lorry_build(lorry_meshes *m)
{
    builder b;
    int s;
    /* Tractor: rear axle at x = 0, front axle at RIG_WHEELBASE. */
    mb_init(&b, tp, tc, ti, TV, TT);
    mb_box(&b, 0.6f, 0.8f, -1.2f, 4.9f, 3.4f, 1.2f, 0xE8402Au);     /* cab */
    mb_box(&b, 4.3f, 1.8f, -1.0f, 5.0f, 2.9f, 1.0f, 0x9FD8FFu);     /* windscreen */
    mb_box(&b, 1.0f, 3.4f, 0.6f, 1.3f, 4.3f, 0.9f, 0xC8C8D0u);      /* exhaust stack */
    mb_box(&b, -1.1f, 0.7f, -1.1f, 0.6f, 1.1f, 1.1f, 0x3A3A44u);    /* chassis and fifth wheel */
    for (s = -1; s <= 1; s += 2) {
        wheel(&b, RIG_WHEELBASE, (float)s);
        wheel(&b, 0.0f, (float)s);
    }
    mb_mesh(&m->tractor, &b);
    /* Trailer: kingpin at x = 0, axles about RIG_TRAILER_LEN behind. */
    mb_init(&b, rp, rc, ri, TV, TT);
    mb_box(&b, -12.6f, 1.2f, -1.28f, 1.0f, 4.2f, 1.28f, 0xFFD23Fu); /* box body */
    mb_box(&b, -12.5f, 4.2f, -1.2f, 0.9f, 4.35f, 1.2f, 0xFFFFFFu);  /* roof trim */
    mb_box(&b, -11.0f, 0.9f, -1.0f, -7.0f, 1.2f, 1.0f, 0x3A3A44u);  /* running gear */
    for (s = -1; s <= 1; s += 2) {
        wheel(&b, -RIG_TRAILER_LEN + 1.4f, (float)s);
        wheel(&b, -RIG_TRAILER_LEN, (float)s);
        wheel(&b, -RIG_TRAILER_LEN - 1.4f, (float)s);
    }
    mb_mesh(&m->trailer, &b);
}

void lorry_draw(const lorry_meshes *m, const rig *r)
{
    float hx, hy;
    glPushMatrix();
    glTranslatef(r->x, 0, -r->y);
    glRotatef(r->heading * 57.29578f, 0, 1, 0);
    dgk_gfx_draw_mesh(&m->tractor);
    glPopMatrix();
    rig_hitch(r, &hx, &hy);
    glPushMatrix();
    glTranslatef(hx, 0, -hy);
    glRotatef(r->trailer_heading * 57.29578f, 0, 1, 0);
    dgk_gfx_draw_mesh(&m->trailer);
    glPopMatrix();
}
