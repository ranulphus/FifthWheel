/* guides.c - reversing guide lines (see guides.h). */
#include "guides.h"
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
