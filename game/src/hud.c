/* hud.c - the heads-up display, in the 640x480 overlay: speed, gear and
 * revs; damage; money; what the job wants next with an arrow the way to
 * go; the job board at a depot; a round minimap of the roads with the
 * route; a docking gauge; big callouts. */
#include "game.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>

#define MM_X 566.0f             /* the minimap: centre, radius (pixels), pixels per metre */
#define MM_Y 404.0f
#define MM_R 62.0f
#define MM_SCALE 0.22f

static void colour(uint32_t rgba)
{
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
}

static void bar(float x, float y, float w, float h, uint32_t rgba)
{
    colour(rgba);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void panel(float x, float y, float w, float h)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bar(x, y, w, h, 0x00000080u);
    glDisable(GL_BLEND);
}

void hud_callout(game *g, const char *text, const char *text2, uint32_t rgba)
{
    g->callout = text;
    g->callout2 = text2;
    g->callout_rgba = rgba;
    g->callout_tick = dgk_app.ticks ? dgk_app.ticks : 1;
}

/* An arrow at (x, y) pointing `a` radians anticlockwise from straight up. */
static void arrow(float x, float y, float a, float size, uint32_t rgba)
{
    static const float shape[4][2] = { { 0, -1.0f }, { 0.7f, 0.4f }, { 0, 0.0f }, { -0.7f, 0.4f } };
    float c = cosf(a), s = sinf(a), p[4][2];
    int pass, i;
    for (pass = 0; pass < 2; pass++) {
        float k = pass ? size : size + 3.0f;
        for (i = 0; i < 4; i++) {           /* screen y is down: anticlockwise on screen */
            p[i][0] = x + (shape[i][0] * c + shape[i][1] * s) * k;
            p[i][1] = y + (-shape[i][0] * s + shape[i][1] * c) * k;
        }
        colour(pass ? rgba : 0x000000FFu);
        glBegin(GL_TRIANGLES);
        glVertex2f(p[0][0], p[0][1]);
        glVertex2f(p[2][0], p[2][1]);
        glVertex2f(p[1][0], p[1][1]);
        glVertex2f(p[0][0], p[0][1]);
        glVertex2f(p[3][0], p[3][1]);
        glVertex2f(p[2][0], p[2][1]);
        glEnd();
    }
}

/* The screen angle of the ground direction (dx, dy) seen by the camera. */
static float bearing(const game *g, float dx, float dy)
{
    float a = atan2f(dy, dx) - g->cam.yaw;
    while (a > WG_PI)
        a -= 2 * WG_PI;
    while (a < -WG_PI)
        a += 2 * WG_PI;
    return a;
}

/* ---- the minimap ------------------------------------------------------- */

static void mm_point(const game *g, float x, float y, float *sx, float *sy)
{
    float dx = x - g->r.x, dy = y - g->r.y, c = cosf(g->cam.yaw), s = sinf(g->cam.yaw);
    *sx = MM_X + (dx * s - dy * c) * MM_SCALE;
    *sy = MM_Y - (dx * c + dy * s) * MM_SCALE;
}

/* The part of a segment inside the minimap's circle; 0 if none. */
static int mm_clip(float *x0, float *y0, float *x1, float *y1)
{
    float ax = *x0 - MM_X, ay = *y0 - MM_Y, dx = *x1 - *x0, dy = *y1 - *y0;
    float a = dx * dx + dy * dy, b = 2 * (ax * dx + ay * dy), c = ax * ax + ay * ay - MM_R * MM_R, disc, t0, t1, q;
    if (a < 1e-6f)
        return c < 0;
    disc = b * b - 4 * a * c;
    if (disc <= 0)
        return 0;
    q = sqrtf(disc);
    t0 = DGK_MAX((-b - q) / (2 * a), 0.0f);
    t1 = DGK_MIN((-b + q) / (2 * a), 1.0f);
    if (t0 >= t1)
        return 0;
    *x1 = *x0 + dx * t1;
    *y1 = *y0 + dy * t1;
    *x0 += dx * t0;
    *y0 += dy * t0;
    return 1;
}

static void mm_line(const game *g, float xa, float ya, float xb, float yb)
{
    float x0, y0, x1, y1;
    mm_point(g, xa, ya, &x0, &y0);
    mm_point(g, xb, yb, &x1, &y1);
    if (mm_clip(&x0, &y0, &x1, &y1)) {
        glVertex2f(x0, y0);
        glVertex2f(x1, y1);
    }
}

/* A square marker, pulled in to the rim when outside the circle (only if
 * `pin`: the destination stays in view). */
static void mm_mark(const game *g, float x, float y, float half, uint32_t rgba, int pin)
{
    float sx, sy, d;
    mm_point(g, x, y, &sx, &sy);
    d = sqrtf((sx - MM_X) * (sx - MM_X) + (sy - MM_Y) * (sy - MM_Y));
    if (d > MM_R - half) {
        if (!pin)
            return;
        sx = MM_X + (sx - MM_X) * (MM_R - half) / d;
        sy = MM_Y + (sy - MM_Y) * (MM_R - half) / d;
    }
    bar(sx - half - 1, sy - half - 1, 2 * half + 2, 2 * half + 2, 0x000000FFu);
    bar(sx - half, sy - half, 2 * half, 2 * half, rgba);
}

static void minimap(const game *g)
{
    const wg_node *n;
    const wg_edge *e;
    const float *p;
    const wg_graph *gr = world_graph(&g->w, &n, &e, &p);
    const float reach = MM_R / MM_SCALE + 30.0f;
    int i, k, dest = -1;
    if (!gr)
        return;
    /* The disc: a fan of 24, translucent. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    colour(0x1E3A28B0u);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(MM_X, MM_Y);
    for (i = 0; i <= 24; i++)
        glVertex2f(MM_X + MM_R * cosf(i * WG_PI / 12), MM_Y + MM_R * sinf(i * WG_PI / 12));
    glEnd();
    glDisable(GL_BLEND);
    /* Roads, every third point (24 m), those near enough. */
    glLineWidth(2.0f);
    colour(0xC8C8D0FFu);
    glBegin(GL_LINES);
    for (i = 0; i < (int)gr->nedges; i++) {
        const float *q = p + e[i].first_point * 2;
        for (k = 0; k + 1 < (int)e[i].npoints; k += 3) {
            int m = DGK_MIN(k + 3, (int)e[i].npoints - 1);
            if (fabsf(q[k * 2] - g->r.x) > reach || fabsf(q[k * 2 + 1] - g->r.y) > reach)
                continue;
            mm_line(g, q[k * 2], q[k * 2 + 1], q[m * 2], q[m * 2 + 1]);
        }
    }
    glEnd();
    /* The route ahead. */
    if (g->jobs.state == JOB_HAULING || g->jobs.state == JOB_DOCKING) {
        const jobs *j = &g->jobs;
        glLineWidth(3.0f);
        colour(0xFFD23FFFu);
        glBegin(GL_LINES);
        for (k = j->route_next; k + 1 < j->nroute && k < j->route_next + 120; k += 2) {
            int m = DGK_MIN(k + 2, j->nroute - 1);
            mm_line(g, j->route[k][0], j->route[k][1], j->route[m][0], j->route[m][1]);
        }
        glEnd();
        dest = j->current.to;
    } else if (g->jobs.state == JOB_TO_PICKUP)
        dest = g->jobs.current.from;
    glLineWidth(1.0f);
    for (i = 0; i < g->w.ndepots; i++)
        if (i != dest)
            mm_mark(g, g->w.depots[i].x, g->w.depots[i].y, 3.0f, 0x3A6FD8FFu, 0);
    if (dest >= 0)
        mm_mark(g, g->w.depots[dest].x, g->w.depots[dest].y, 4.0f, 0xFF5030FFu, 1);
    /* The rig: an arrow the way it faces. */
    arrow(MM_X, MM_Y, bearing(g, cosf(g->r.heading), sinf(g->r.heading)), 6.0f, 0xFFFFFFFFu);
    /* The rim. */
    glLineWidth(2.0f);
    colour(0xFFFFFFFFu);
    glBegin(GL_LINES);
    for (i = 0; i < 24; i++) {
        glVertex2f(MM_X + MM_R * cosf(i * WG_PI / 12), MM_Y + MM_R * sinf(i * WG_PI / 12));
        glVertex2f(MM_X + MM_R * cosf((i + 1) * WG_PI / 12), MM_Y + MM_R * sinf((i + 1) * WG_PI / 12));
    }
    glEnd();
    glLineWidth(1.0f);
}

/* ---- the reversing camera ----------------------------------------------- */

/* The look of a car's rear camera: the picture's edges darkened, viewfinder
 * brackets in the corners, a label with a blinking dot. */
static void rear_cam(game *g)
{
    static const float inset = 26, len = 44, thick = 5;
    int c;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bar(0, 0, 640, 14, 0x00000070u);
    bar(0, 466, 640, 14, 0x00000070u);
    bar(0, 14, 14, 452, 0x00000070u);
    bar(626, 14, 14, 452, 0x00000070u);
    glDisable(GL_BLEND);
    for (c = 0; c < 4; c++) {
        float x = (c & 1) ? 640 - inset : inset, y = (c & 2) ? 480 - inset : inset;
        float dx = (c & 1) ? -1.0f : 1.0f, dy = (c & 2) ? -1.0f : 1.0f;
        int pass;
        for (pass = 0; pass < 2; pass++) {             /* a dark shadow, then white */
            float o = pass ? 0 : 2;
            uint32_t rgba = pass ? 0xFFFFFFFFu : 0x000000FFu;
            bar(DGK_MIN(x, x + dx * len) + o, DGK_MIN(y, y + dy * thick) + o, len, thick, rgba);
            bar(DGK_MIN(x, x + dx * thick) + o, DGK_MIN(y, y + dy * len) + o, thick, len, rgba);
        }
    }
    {
        const char *label = "REAR CAM";
        float w = dgk_text_width(&g->font, 1.0f, label), x = 320 - w / 2 + 8;
        if ((dgk_app.ticks / 30) & 1) {
            bar(x - 18, 20, 10, 10, 0x000000FFu);
            bar(x - 17, 21, 8, 8, 0xFF3030FFu);
        }
        dgk_text(&g->font, x, 16, 1.0f, 0xFFFFFFFFu, label);
    }
}

/* ---- the job ------------------------------------------------------------ */

static void job_board(game *g)
{
    const jobs *j = &g->jobs;
    char line[64];
    int i;
    panel(14, 44, 380, 30 + 22 * JOB_OFFERS + 22);
    snprintf(line, sizeof line, "DEPOT %d: A %s TO TAKE", j->offers_at + 1,
             trailer_name[j->trailers[j->offers_at].type]);
    dgk_text(&g->font, 22, 50, 1.0f, 0xFFD23FFFu, line);
    for (i = 0; i < JOB_OFFERS; i++) {
        const job *o = &j->offers[i];
        snprintf(line, sizeof line, "%d  DEPOT %d BAY %d  %4.1f KM  $%d", i + 1, o->to + 1, o->bay + 1,
                 o->distance / 1000.0f, (int)o->pay);
        dgk_text(&g->font, 22, 76 + 22 * i, 1.0f, 0xFFFFFFFFu, line);
    }
    dgk_text(&g->font, 22, 76 + 22 * JOB_OFFERS, 1.0f, 0x9FFFB0FFu, "PRESS 1, 2 OR 3");
}

static void dock_gauge(game *g)
{
    const jobs *j = &g->jobs;
    char line[32];
    float lat = DGK_CLAMP(j->lat, -2.0f, 2.0f), a = j->angle * 180.0f / WG_PI;
    int grade = jobs_grade(j->lat, j->gap, j->angle);
    static const uint32_t grade_rgba[] = { 0xFF5030FFu, 0xFFFFFFFFu, 0x9FFFB0FFu, 0x5FD8FFFFu, 0xFFD23FFFu };
    /* Left, under the job line: the dock stays in view. The sideways bar is
     * as seen from behind the trailer (the camera looks into the bay). */
    panel(14, 76, 250, 78);
    snprintf(line, sizeof line, "GAP %5.1f M", DGK_MAX(j->gap, 0.0f));
    dgk_text(&g->font, 22, 82, 1.0f, j->gap < 0.5f ? 0xFFD23FFFu : 0xFFFFFFFFu, line);
    bar(34, 110, 160, 6, 0x55555FFFu);
    bar(112, 104, 4, 18, 0xFFFFFFFFu);
    bar(112 + lat * 40 - 4, 102, 8, 22, fabsf(j->lat) < 0.5f ? 0x9FFFB0FFu : 0xFF5030FFu);
    snprintf(line, sizeof line, "ANGLE %4.1f", a);
    dgk_text(&g->font, 22, 130, 1.0f, fabsf(a) < 4.0f ? 0x9FFFB0FFu : 0xFFFFFFFFu, line);
    if (grade)
        dgk_text(&g->font, 256 - dgk_text_width(&g->font, 1.0f, grade_name[grade]), 130, 1.0f, grade_rgba[grade],
                 grade_name[grade]);
}

static void job_panel(game *g)
{
    const jobs *j = &g->jobs;
    char line[64];
    float tx = 0, ty = 0;
    int point = 1;
    switch (j->state) {
    case JOB_NONE:
        if (j->at_depot >= 0 && j->offers_at == j->at_depot && j->trailers[j->at_depot].present) {
            job_board(g);
            point = 0;
        } else {
            int i, best = -1;
            float bd = 1e30f;
            for (i = 0; i < g->w.ndepots; i++) {
                float d = (g->w.depots[i].x - g->r.x) * (g->w.depots[i].x - g->r.x) +
                          (g->w.depots[i].y - g->r.y) * (g->w.depots[i].y - g->r.y);
                if (d < bd && j->trailers[i].present && i != j->at_depot) {
                    bd = d;
                    best = i;
                }
            }
            panel(14, 44, 380, 26);
            if (best < 0) {
                point = 0;
                break;
            }
            snprintf(line, sizeof line, "WORK AT DEPOT %d  %.1f KM", best + 1, sqrtf(bd) / 1000.0f);
            dgk_text(&g->font, 22, 50, 1.0f, 0xFFFFFFFFu, line);
            tx = g->w.depots[best].x;
            ty = g->w.depots[best].y;
        }
        break;
    case JOB_TO_PICKUP:
        panel(14, 44, 380, 48);
        snprintf(line, sizeof line, "BACK UNDER THE %s", trailer_name[j->current.type]);
        dgk_text(&g->font, 22, 50, 1.0f, 0xFFFFFFFFu, line);
        dgk_text(&g->font, 22, 72, 1.0f, 0x9FFFB0FFu, "BACKSPACE: CANCEL");
        tx = j->trailers[j->current.from].x;
        ty = j->trailers[j->current.from].y;
        break;
    case JOB_HAULING:
        panel(14, 44, 380, 26);
        snprintf(line, sizeof line, "TO DEPOT %d BAY %d  %.1f KM", j->current.to + 1, j->current.bay + 1,
                 DGK_MAX(j->route_left, 0.0f) / 1000.0f);
        dgk_text(&g->font, 22, 50, 1.0f, 0xFFFFFFFFu, line);
        if (j->route_next + 4 < j->nroute) {
            tx = j->route[j->route_next + 4][0];
            ty = j->route[j->route_next + 4][1];
        } else {
            tx = g->w.depots[j->current.to].x;
            ty = g->w.depots[j->current.to].y;
        }
        break;
    case JOB_DOCKING: {
        float hd;
        panel(14, 44, 380, 26);
        snprintf(line, sizeof line, "BACK INTO BAY %d", j->current.bay + 1);
        dgk_text(&g->font, 22, 50, 1.0f, 0xFFFFFFFFu, line);
        jobs_bay(j, &g->w, &tx, &ty, &hd);
        dock_gauge(g);
        break;
    }
    }
    if (point && !g->rearcam)                          /* reversing, the camera looks the way to go */
        arrow(320, 40, bearing(g, tx - g->r.x, ty - g->r.y), 16.0f, 0xFFD23FFFu);
}

void hud_draw(game *g)
{
    char line[64], gbuf[12];
    const char *gear = gbuf;
    uint64_t now;
    if (g->r.gear < 0)
        gear = "R";
    else
        snprintf(gbuf, sizeof gbuf, "%d", g->r.gear);
    dgk_gfx_overlay_begin();
    if (g->rearcam)
        rear_cam(g);
    panel(18, 424, 204, 40);
    bar(22, 452, 196 * DGK_CLAMP((g->r.rpm - 600.0f) / 1600.0f, 0.0f, 1.0f), 8,
        g->r.rpm > 1900 ? 0xFF5030FFu : 0x9FFFB0FFu);
    snprintf(line, sizeof line, "%3.0f KM/H  GEAR %s", fabsf(g->r.v) * 3.6f, gear);
    dgk_text(&g->font, 24, 428, 1.0f, 0xFFFFFFFFu, line);
    snprintf(line, sizeof line, "DAMAGE %3.0f%%", g->r.damage);
    dgk_text(&g->font, 24, 400, 1.0f, g->r.damage > 50 ? 0xFF6040FFu : 0xFFFFFFFFu, line);
    if (g->mode == MODE_TOUR) {
        snprintf(line, sizeof line, "AUTOPILOT  LAP %d", g->ap.laps + 1);
        dgk_text(&g->font, 20, 16, 1.0f, 0xFFD23FFFu, line);
    } else if (g->mode == MODE_JOB)
        dgk_text(&g->font, 20, 16, 1.0f, 0xFFD23FFFu, "AUTOPILOT");
    if (g->has_jobs) {
        snprintf(line, sizeof line, "$%d", g->jobs.money);
        dgk_text(&g->font, 620 - dgk_text_width(&g->font, 1.5f, line), 12, 1.5f, 0xFFD23FFFu, line);
        job_panel(g);
        minimap(g);
    }
    if (g->callout && dgk_app.ticks - g->callout_tick < 150) {
        float t = (dgk_app.ticks - g->callout_tick) / 150.0f, s = 2.0f + 0.6f * sinf(t * 30.0f) * (1 - t) * (1 - t);
        dgk_text(&g->font, 320 - dgk_text_width(&g->font, s, g->callout) / 2, 180, s, g->callout_rgba, g->callout);
        if (g->callout2)
            dgk_text(&g->font, 320 - dgk_text_width(&g->font, 1.5f, g->callout2) / 2, 180 + 22 * s, 1.5f,
                     0xFFD23FFFu, g->callout2);
    }
    if (!dgk_app.fixed) {
        now = dgk_now_us();
        if (++g->fps_frames >= 30 || now - g->fps_t0 > 1000000) {
            g->fps = g->fps_frames * 1e6f / (float)(now - g->fps_t0 ? now - g->fps_t0 : 1);
            g->fps_frames = 0;
            g->fps_t0 = now;
        }
        snprintf(line, sizeof line, "%.1f FPS", g->fps);
    } else
        snprintf(line, sizeof line, "TICK %lu", (unsigned long)dgk_app.ticks);
    dgk_text(&g->font, 250, 456, 1.0f, 0x9FFFB0FFu, line);
    dgk_gfx_overlay_end();
}
