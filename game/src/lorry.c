/* lorry.c - the toy lorry: a stubby cab, big wheels; a tall box trailer, a
 * flatbed with crates, a tanker. */
#include "lorry.h"
#include <GL/gl.h>
#include <math.h>

#define TV 512
#define TT 512
static float tp[TV * 3], rp[TRAILER_TYPES][TV * 3];
static uint8_t tc[TV * 4], rc[TRAILER_TYPES][TV * 4];
static uint16_t ti[TT * 3], ri[TRAILER_TYPES][TT * 3];

/* A wheel: a dark tyre block with a bright hub, x along the lorry. */
static void wheel(builder *b, float x, float side)
{
    float z0 = side < 0 ? -1.55f : 1.05f;
    mb_box(b, x - 0.65f, 0.0f, z0, x + 0.65f, 1.3f, z0 + 0.5f, 0x26262Cu);
    mb_box(b, x - 0.25f, 0.45f, side < 0 ? z0 - 0.05f : z0 + 0.5f, x + 0.25f, 0.85f, side < 0 ? z0 : z0 + 0.55f,
           0xD0D4DCu);
}

/* An octagonal tank along x, its ends in their own colour. */
static void tank(builder *b, float x0, float x1, float cy, float r, uint32_t rgb, uint32_t ends)
{
    float ring[8][2], p[4][3], q[4][3];
    int i, k;
    for (i = 0; i < 8; i++) {
        float a = (i + 0.5f) * 3.14159265f / 4;
        ring[i][0] = cy + r * cosf(a);
        ring[i][1] = r * sinf(a);
    }
    for (i = 0; i < 8; i++) {
        int n = (i + 1) & 7;
        float side[4][3] = { { x0, ring[i][0], ring[i][1] }, { x0, ring[n][0], ring[n][1] },
                             { x1, ring[n][0], ring[n][1] }, { x1, ring[i][0], ring[i][1] } };
        mb_quad(b, side[0], side[1], side[2], side[3], rgb);
    }
    {
        static const int caps[3][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 3, 4, 7 } };
        for (i = 0; i < 3; i++) {
            for (k = 0; k < 4; k++) {
                int f = caps[i][k], bk = caps[i][3 - k];
                p[k][0] = x1; p[k][1] = ring[f][0]; p[k][2] = ring[f][1];         /* front, facing +x */
                q[k][0] = x0; q[k][1] = ring[bk][0]; q[k][2] = ring[bk][1];       /* back, facing -x */
            }
            mb_quad(b, p[0], p[1], p[2], p[3], ends);
            mb_quad(b, q[0], q[1], q[2], q[3], ends);
        }
    }
}

void lorry_build(lorry_meshes *m)
{
    builder b;
    int s, t;
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
    /* Trailers: kingpin at x = 0, axles about RIG_TRAILER_LEN behind. */
    for (t = 0; t < TRAILER_TYPES; t++) {
        mb_init(&b, rp[t], rc[t], ri[t], TV, TT);
        if (t == TRAILER_BOX) {
            mb_box(&b, -12.6f, 1.2f, -1.28f, 1.0f, 4.2f, 1.28f, 0xFFD23Fu);    /* box body */
            mb_box(&b, -12.5f, 4.2f, -1.2f, 0.9f, 4.35f, 1.2f, 0xFFFFFFu);     /* roof trim */
        } else if (t == TRAILER_FLATBED) {
            mb_box(&b, -12.6f, 1.2f, -1.28f, 1.0f, 1.55f, 1.28f, 0x3A6FD8u);   /* deck */
            mb_box(&b, 0.6f, 1.55f, -1.2f, 1.0f, 3.2f, 1.2f, 0xC8C8D0u);       /* headboard */
            mb_box(&b, -12.0f, 1.55f, -1.1f, -7.2f, 3.3f, 1.1f, 0xE0913Cu);    /* crates */
            mb_box(&b, -6.4f, 1.55f, -1.1f, -1.6f, 2.8f, 1.1f, 0xF2B45Au);
            mb_box(&b, -9.8f, 3.3f, -1.15f, -9.4f, 3.36f, 1.15f, 0x26262Cu);   /* straps */
            mb_box(&b, -4.2f, 2.8f, -1.15f, -3.8f, 2.86f, 1.15f, 0x26262Cu);
        } else {
            mb_box(&b, -12.6f, 1.1f, -0.9f, 1.0f, 1.4f, 0.9f, 0x3A3A44u);      /* chassis */
            tank(&b, -12.4f, 0.6f, 2.65f, 1.3f, 0xDCE3EAu, 0xE8402Au);
            mb_box(&b, -10.0f, 3.9f, -0.35f, -2.0f, 4.05f, 0.35f, 0x8A8F99u);  /* walkway */
        }
        mb_box(&b, -11.0f, 0.9f, -1.0f, -7.0f, 1.2f, 1.0f, 0x3A3A44u);         /* running gear */
        mb_box(&b, -2.4f, 0.0f, -1.0f, -2.1f, 1.2f, -0.7f, 0x55555Fu);         /* landing legs */
        mb_box(&b, -2.4f, 0.0f, 0.7f, -2.1f, 1.2f, 1.0f, 0x55555Fu);
        for (s = -1; s <= 1; s += 2) {
            wheel(&b, -RIG_TRAILER_LEN + 1.4f, (float)s);
            wheel(&b, -RIG_TRAILER_LEN, (float)s);
            wheel(&b, -RIG_TRAILER_LEN - 1.4f, (float)s);
        }
        mb_mesh(&m->trailer[t], &b);
    }
}

/* Posed on the ground: the tractor pitched between its axles and rolled
 * across its wheels, the trailer pitched from the fifth wheel to its axles
 * (the physics stays flat: this is only how it looks). */
void lorry_draw(const lorry_meshes *m, const rig *r, const world *w)
{
    float c = cosf(r->heading), s = sinf(r->heading), hx, hy, ax, ay;
    float h_rear = world_height(w, r->x, r->y);
    float h_front = world_height(w, r->x + RIG_WHEELBASE * c, r->y + RIG_WHEELBASE * s);
    float h_left = world_height(w, r->x + 1.9f * c - 1.2f * s, r->y + 1.9f * s + 1.2f * c);
    float h_right = world_height(w, r->x + 1.9f * c + 1.2f * s, r->y + 1.9f * s - 1.2f * c);
    float pitch = atan2f(h_front - h_rear, RIG_WHEELBASE) * 57.29578f;
    float roll = atan2f(h_left - h_right, 2.4f) * 57.29578f;
    float h_hitch;
    glPushMatrix();
    glTranslatef(r->x, h_rear, -r->y);
    glRotatef(r->heading * 57.29578f, 0, 1, 0);
    glRotatef(pitch, 0, 0, 1);
    glRotatef(-roll, 1, 0, 0);
    dgk_gfx_draw_mesh(&m->tractor);
    glPopMatrix();
    if (!r->has_trailer)
        return;
    rig_hitch(r, &hx, &hy);
    rig_trailer_axle(r, &ax, &ay);
    h_hitch = h_rear + (h_front - h_rear) * RIG_HITCH / RIG_WHEELBASE;
    glPushMatrix();
    glTranslatef(hx, h_hitch, -hy);
    glRotatef(r->trailer_heading * 57.29578f, 0, 1, 0);
    glRotatef(atan2f(h_hitch - world_height(w, ax, ay), RIG_TRAILER_LEN) * 57.29578f, 0, 0, 1);
    dgk_gfx_draw_mesh(&m->trailer[r->trailer_type]);
    glPopMatrix();
}

void lorry_draw_trailer(const lorry_meshes *m, int type, float kx, float ky, float heading, const world *w)
{
    float h = world_height(w, kx, ky);
    float ah = world_height(w, kx - RIG_TRAILER_LEN * cosf(heading), ky - RIG_TRAILER_LEN * sinf(heading));
    glPushMatrix();
    glTranslatef(kx, h, -ky);
    glRotatef(heading * 57.29578f, 0, 1, 0);
    glRotatef(atan2f(h - ah, RIG_TRAILER_LEN) * 57.29578f, 0, 0, 1);
    dgk_gfx_draw_mesh(&m->trailer[type]);
    glPopMatrix();
}
