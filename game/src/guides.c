/* guides.c - reversing guide lines (see guides.h). */
#include "guides.h"
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <math.h>

#define STEPS 20
#define LENGTH 10.0f            /* metres of path shown */
#define HALF 0.17f              /* half a line's width */
#define EDGE 0.07f              /* the dark outline around it */
#define LIFT 0.06f              /* above the ground */

static const float bars[3] = { 1.5f, 4.0f, 10.0f };

/* Red near the back, yellow, then green. */
static void colour_at(float metres)
{
    if (metres < bars[0])
        glColor4ub(255, 70, 50, 220);
    else if (metres < bars[1])
        glColor4ub(255, 210, 63, 210);
    else
        glColor4ub(95, 224, 122, 190);
}

static void vertex(const world *w, float x, float y)
{
    glVertex3f(x, world_height(w, x, y) + LIFT, -y);
}

/* One line along a predicted path: a strip of quads `half` either side,
 * coloured by distance, or dark (the outline pass). */
static void line(const float (*p)[2], const world *w, float half, int outline)
{
    int i;
    for (i = 0; i < STEPS; i++) {
        float dx = p[i + 1][0] - p[i][0], dy = p[i + 1][1] - p[i][1], l = sqrtf(dx * dx + dy * dy);
        float nx = l > 1e-4f ? -dy / l * half : 0, ny = l > 1e-4f ? dx / l * half : 0;
        if (outline)
            glColor4ub(0, 0, 0, 150);
        else
            colour_at((i + 0.5f) * LENGTH / STEPS);
        vertex(w, p[i][0] + nx, p[i][1] + ny);
        vertex(w, p[i][0] - nx, p[i][1] - ny);
        vertex(w, p[i + 1][0] - nx, p[i + 1][1] - ny);
        vertex(w, p[i + 1][0] + nx, p[i + 1][1] + ny);
    }
}

void guides_draw(const rig *r, const world *w)
{
    float left[STEPS + 1][2], right[STEPS + 1][2];
    int b;
    rig_predict_reverse(r, LENGTH, STEPS, left, right);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    line((const float (*)[2])left, w, HALF + EDGE, 1);
    line((const float (*)[2])right, w, HALF + EDGE, 1);
    line((const float (*)[2])left, w, HALF, 0);
    line((const float (*)[2])right, w, HALF, 0);
    /* Bars across: at each distance, from one line to the other, thickened back towards the rig. */
    for (b = 0; b < 3; b++) {
        int i = (int)(bars[b] / LENGTH * STEPS + 0.5f), j = i - 1;
        float ax = left[i][0], ay = left[i][1], bx = right[i][0], by = right[i][1];
        float tx = (left[j][0] + right[j][0] - ax - bx) * 0.5f, ty = (left[j][1] + right[j][1] - ay - by) * 0.5f;
        float tl = sqrtf(tx * tx + ty * ty), k = tl > 1e-4f ? HALF * 2.0f / tl : 0;
        colour_at(bars[b] - 0.01f);
        vertex(w, ax, ay);
        vertex(w, bx, by);
        vertex(w, bx + tx * k, by + ty * k);
        vertex(w, ax + tx * k, ay + ty * k);
    }
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

/* ---- the route's chevrons -------------------------------------------------- */

#define CHEV_FIRST 1            /* road points ahead of the nearest: the first tried */
#define CHEV_COUNT 7            /* tried: enough to reach CHEV_FAR at 8 m apart */
#define CHEV_NEAR 7.0f          /* metres from the rig (its rear axle): fading in from here... */
#define CHEV_IN 4.0f            /* ...over this */
#define CHEV_FAR 48.0f          /* and out to here... */
#define CHEV_OUT 12.0f          /* ...over this */
#define CHEV_LIFT 0.3f          /* above the ground: about where a road's surface is */
#define CHEV_HALF 1.6f          /* half the chevron's span across the road */
#define CHEV_LEN 0.9f           /* tip to arms' ends, along the road, halved */
#define CHEV_THICK 0.8f         /* each arm, along the road */
#define CHEV_PERIOD 40          /* ticks to slide from one road point to the next */

static void chev_vertex(const world *w, float x, float y)
{
    glVertex3f(x, world_height(w, x, y) + CHEV_LIFT, -y);
}

void route_draw(const jobs *j, const rig *r, const world *w, uint32_t ticks)
{
    float slide = (float)(ticks % CHEV_PERIOD) / CHEV_PERIOD, dx, dy;
    int k;
    if (j->state != JOB_HAULING || j->route_next >= j->nroute)
        return;
    dx = j->route[j->route_next][0] - r->x;
    dy = j->route[j->route_next][1] - r->y;
    if (dx * dx + dy * dy > 30.0f * 30.0f)          /* off the route: it will be worked out again */
        return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    for (k = 0; k < CHEV_COUNT; k++) {
        int i = j->route_next + CHEV_FIRST + k;
        float ax, ay, bx, by, l, ux, uy, nx, ny, px, py, d, fade;
        if (i + 1 >= j->nroute)
            break;
        ax = j->route[i][0];
        ay = j->route[i][1];
        bx = j->route[i + 1][0];
        by = j->route[i + 1][1];
        l = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        if (l < 0.01f)
            continue;
        ux = (bx - ax) / l;
        uy = (by - ay) / l;
        nx = -uy;
        ny = ux;
        px = ax + (bx - ax) * slide;
        py = ay + (by - ay) * slide;
        /* By its distance from the rig, so it does not jump as the route's nearest point moves on. */
        d = sqrtf((px - r->x) * (px - r->x) + (py - r->y) * (py - r->y));
        fade = DGK_CLAMP((d - CHEV_NEAR) / CHEV_IN, 0.0f, 1.0f) * DGK_CLAMP((CHEV_FAR - d) / CHEV_OUT, 0.0f, 1.0f);
        if (fade <= 0.0f)
            continue;
        glColor4ub(255, 210, 63, (GLubyte)(230.0f * fade));
        /* The left arm, then the right: from the tip back to each end. */
        chev_vertex(w, px + ux * CHEV_LEN, py + uy * CHEV_LEN);
        chev_vertex(w, px - ux * CHEV_LEN + nx * CHEV_HALF, py - uy * CHEV_LEN + ny * CHEV_HALF);
        chev_vertex(w, px - ux * (CHEV_LEN + CHEV_THICK) + nx * CHEV_HALF,
                    py - uy * (CHEV_LEN + CHEV_THICK) + ny * CHEV_HALF);
        chev_vertex(w, px + ux * (CHEV_LEN - CHEV_THICK), py + uy * (CHEV_LEN - CHEV_THICK));
        chev_vertex(w, px + ux * CHEV_LEN, py + uy * CHEV_LEN);
        chev_vertex(w, px + ux * (CHEV_LEN - CHEV_THICK), py + uy * (CHEV_LEN - CHEV_THICK));
        chev_vertex(w, px - ux * (CHEV_LEN + CHEV_THICK) - nx * CHEV_HALF,
                    py - uy * (CHEV_LEN + CHEV_THICK) - ny * CHEV_HALF);
        chev_vertex(w, px - ux * CHEV_LEN - nx * CHEV_HALF, py - uy * CHEV_LEN - ny * CHEV_HALF);
        dgk_gfx_tris += 4;
    }
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

/* ---- the target's markers -------------------------------------------------- */

#define BAY_HALF 1.6f           /* the bay's lines either side of its middle */
#define BAY_LEN 19.5f           /* from the dock out to the lines' ends */
#define BAY_NEAR 150.0f         /* metres from the bay: the markers show */

/* The bay's middle line: from (bx, by) at the dock, out along heading bh. */
static int target_bay(const jobs *j, const rig *r, const world *w, float *bx, float *by, float *bh)
{
    if (j->state != JOB_HAULING && j->state != JOB_DOCKING)
        return 0;
    jobs_bay(j, w, bx, by, bh);
    return (*bx - r->x) * (*bx - r->x) + (*by - r->y) * (*by - r->y) < BAY_NEAR * BAY_NEAR;
}

void bay_draw(const jobs *j, const rig *r, const world *w, uint32_t ticks)
{
    float bx, by, bh, c, s, pulse = 0.5f + 0.5f * sinf((float)ticks * 0.07f);
    static const float edge[4][4] = { { -0.5f, -BAY_HALF, BAY_LEN, -BAY_HALF }, { BAY_LEN, -BAY_HALF, BAY_LEN, BAY_HALF },
                                      { BAY_LEN, BAY_HALF, -0.5f, BAY_HALF }, { -0.5f, BAY_HALF, -0.5f, -BAY_HALF } };
    int k;
    if (!target_bay(j, r, w, &bx, &by, &bh))
        return;
    c = cosf(bh);
    s = sinf(bh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    glColor4ub(255, 210, 63, (GLubyte)(110.0f + 120.0f * pulse));
    k = 0;
    if (j->current.kind == JOB_KIND_PARCEL) {
        /* A parcel stop: a ring on the street the cab stops in (4 m, the drop's reach), breathing a little. */
        float ro = 4.0f + 0.25f * pulse, ri = ro - 0.45f;
        for (k = 0; k < 24; k++) {
            float a0 = k * (2 * WG_PI / 24), a1 = (k + 1) * (2 * WG_PI / 24);
            float q[4][2] = { { bx + ri * cosf(a0), by + ri * sinf(a0) }, { bx + ro * cosf(a0), by + ro * sinf(a0) },
                              { bx + ro * cosf(a1), by + ro * sinf(a1) }, { bx + ri * cosf(a1), by + ri * sinf(a1) } };
            int m;
            for (m = 0; m < 4; m++)
                glVertex3f(q[m][0], world_height(w, q[m][0], q[m][1]) + CHEV_LIFT, -q[m][1]);
            dgk_gfx_tris += 2;
        }
        k = 4;                                       /* (no bay outline) */
    }
    for (; k < 4; k++) {
        /* A strip 0.35 m wide inside each side of the bay's rectangle. */
        float a0 = edge[k][0], c0 = edge[k][1], a1 = edge[k][2], c1 = edge[k][3];
        float da = a1 - a0, dc = c1 - c0, l = sqrtf(da * da + dc * dc), ia = -dc / l * 0.35f, ic = da / l * 0.35f;
        float p[4][2] = { { a0, c0 }, { a1, c1 }, { a1 + ia, c1 + ic }, { a0 + ia, c0 + ic } };
        int q;
        for (q = 0; q < 4; q++) {
            float x = bx + c * p[q][0] - s * p[q][1], y = by + s * p[q][0] + c * p[q][1];
            glVertex3f(x, world_height(w, x, y) + CHEV_LIFT, -y);
        }
        dgk_gfx_tris += 2;
    }
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

/* A gold arrow pointing down at (x, y), its tip `up` metres above the
 * ground, turned `spin` radians: a four-sided head and a square shaft,
 * each side a shade of its own. */
static void arrow3d(const world *w, float x, float y, float up, float spin)
{
    static const float shade[4] = { 1.0f, 0.82f, 0.68f, 0.86f };
    float g = world_height(w, x, y) + up;
    int k;
    glPushMatrix();
    glTranslatef(x, g, -y);
    glRotatef(spin * 57.29578f, 0, 1, 0);
    glDisable(GL_CULL_FACE);
    glBegin(GL_TRIANGLES);
    dgk_gfx_draws++;
    for (k = 0; k < 4; k++) {
        float a0 = k * 1.5707963f, a1 = (k + 1) * 1.5707963f;
        float x0 = cosf(a0) * 1.0f, z0 = sinf(a0) * 1.0f, x1 = cosf(a1) * 1.0f, z1 = sinf(a1) * 1.0f;
        float sx0 = cosf(a0) * 0.38f, sz0 = sinf(a0) * 0.38f, sx1 = cosf(a1) * 0.38f, sz1 = sinf(a1) * 0.38f;
        GLubyte r = (GLubyte)(255 * shade[k]), gg = (GLubyte)(210 * shade[k]), b = (GLubyte)(63 * shade[k]);
        glColor3ub(r, gg, b);
        glVertex3f(0, 0, 0);                         /* the head: from the tip up to its rim */
        glVertex3f(x0, 1.3f, z0);
        glVertex3f(x1, 1.3f, z1);
        glVertex3f(sx0, 1.3f, sz0);                  /* the shaft's side */
        glVertex3f(sx0, 2.9f, sz0);
        glVertex3f(sx1, 2.9f, sz1);
        glVertex3f(sx0, 1.3f, sz0);
        glVertex3f(sx1, 2.9f, sz1);
        glVertex3f(sx1, 1.3f, sz1);
    }
    glColor3ub(255, 236, 140);                       /* the head's rim and the shaft's top, lit */
    for (k = 1; k < 3; k++) {
        glVertex3f(1.0f, 1.3f, 0);
        glVertex3f(cosf(k * 1.5707963f), 1.3f, sinf(k * 1.5707963f));
        glVertex3f(cosf((k + 1) * 1.5707963f), 1.3f, sinf((k + 1) * 1.5707963f));
        glVertex3f(0.38f, 2.9f, 0);
        glVertex3f(cosf(k * 1.5707963f) * 0.38f, 2.9f, sinf(k * 1.5707963f) * 0.38f);
        glVertex3f(cosf((k + 1) * 1.5707963f) * 0.38f, 2.9f, sinf((k + 1) * 1.5707963f) * 0.38f);
    }
    glEnd();
    dgk_gfx_tris += 16;
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}

void target_arrow_draw(const jobs *j, const rig *r, const world *w, uint32_t ticks)
{
    float t = (float)ticks / 60.0f, bob = 0.7f * fabsf(sinf(t * 2.6f)), bx, by, bh;
    if (j->state == JOB_TO_PICKUP) {
        const parked *p = j->current.kind == JOB_KIND_SHUNT && j->current.way == SHUNT_TO_LINE
                              ? &j->docked[j->current.from] : &j->trailers[j->current.from];
        if (p->present)
            arrow3d(w, p->x - 1.0f * cosf(p->heading), p->y - 1.0f * sinf(p->heading), 5.6f + bob, t * 2.0f);
        return;
    }
    if (target_bay(j, r, w, &bx, &by, &bh) && j->current.kind == JOB_KIND_PARCEL)
        arrow3d(w, bx, by, 4.5f + bob, t * 2.0f);      /* over the parcel's stop */
    else if (target_bay(j, r, w, &bx, &by, &bh) && j->state != JOB_DOCKING)
        arrow3d(w, bx + 9.0f * cosf(bh), by + 9.0f * sinf(bh), 5.0f + bob, t * 2.0f);
}

