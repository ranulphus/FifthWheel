/* townroute.c - ways along a town's streets (see townroute.h). */
#include "townroute.h"
#include <math.h>
#include <string.h>

#define STEP 6.0f               /* metres between the points given back */
#define FINE 1.0f               /* while working */
#define WORK_MAX 4096
#define TURN 0.26f              /* radians: a bend kept as a point of its own (15 degrees) */
#define ROUND 10.0f             /* a street corner's radius on the streets' middles (left lane 7.75 m, right 12.25) */

static void to_frame(const wg_town *t, float x, float y, float *u, float *v)
{
    float c = cosf(t->heading), s = sinf(t->heading), dx = x - t->x, dy = y - t->y;
    *u = dx * c + dy * s;
    *v = -dx * s + dy * c;
}

static void from_frame(const wg_town *t, float u, float v, float *x, float *y)
{
    float c = cosf(t->heading), s = sinf(t->heading);
    *x = t->x + c * u - s * v;
    *y = t->y + s * u + c * v;
}

int town_at(const wg_town *towns, int ntowns, float x, float y, float margin)
{
    int i, best = -1;
    float bd = 1e30f;
    for (i = 0; i < ntowns; i++) {
        float u, v, ext = (float)towns[i].blocks * WG_GRID + margin;
        to_frame(&towns[i], x, y, &u, &v);
        if (fabsf(u) <= ext && fabsf(v) <= ext && u * u + v * v < bd) {
            bd = u * u + v * v;
            best = i;
        }
    }
    return best;
}

/* A street: axis 0 is u = k * WG_GRID (it runs along v), axis 1 is v = k * WG_GRID (along u). */
typedef struct sline {
    int axis, k;
} sline;

static int iclamp(int a, int lo, int hi)
{
    return a < lo ? lo : a > hi ? hi : a;
}

static float fclamp(float a, float lo, float hi)
{
    return a < lo ? lo : a > hi ? hi : a;
}

/* The street nearest (u, v), and how far along it the point is. */
static sline snap(const wg_town *t, float u, float v, float *along)
{
    int b = (int)t->blocks, ku = iclamp((int)floorf(u / WG_GRID + 0.5f), -b, b);
    int kv = iclamp((int)floorf(v / WG_GRID + 0.5f), -b, b);
    float ext = (float)b * WG_GRID;
    sline l;
    if (fabsf(u - ku * WG_GRID) <= fabsf(v - kv * WG_GRID)) {
        l.axis = 0;
        l.k = ku;
        *along = fclamp(v, -ext, ext);
    } else {
        l.axis = 1;
        l.k = kv;
        *along = fclamp(u, -ext, ext);
    }
    return l;
}

static void at(sline l, float along, float *p)
{
    p[0] = l.axis == 0 ? l.k * WG_GRID : along;
    p[1] = l.axis == 0 ? along : l.k * WG_GRID;
}

/* Points every FINE metres from a to b (a included, b not), appended to w. */
static int fill(float (*w)[2], int n, const float *a, const float *b)
{
    float dx = b[0] - a[0], dy = b[1] - a[1], l = sqrtf(dx * dx + dy * dy);
    int k, m = (int)(l / FINE);
    for (k = 0; k < m && n < WORK_MAX - 1; k++, n++) {
        w[n][0] = a[0] + dx * (float)k / (float)m;
        w[n][1] = a[1] + dy * (float)k / (float)m;
    }
    return n;
}

/* Where the segment from inside the ring to outside (or back) crosses its circle. */
static void crossing(const float *in, const float *out, float *p)
{
    float lo = 0, hi = 1;
    int k;
    for (k = 0; k < 20; k++) {
        float m = (lo + hi) * 0.5f, x = in[0] + (out[0] - in[0]) * m, y = in[1] + (out[1] - in[1]) * m;
        if (x * x + y * y < WG_RING_R * WG_RING_R)
            lo = m;
        else
            hi = m;
    }
    p[0] = in[0] + (out[0] - in[0]) * hi;
    p[1] = in[1] + (out[1] - in[1]) * hi;
}

/* The corners of the way from street l0 (a0 along it), facing (fu, fv), to street l1 (a1 along it): into c
 * (at most 4), their count. */
static int corners(const wg_town *t, sline l0, float a0, float fu, float fv, sline l1, float a1, float (*corner)[2])
{
    int nc = 0, b = (int)t->blocks;
    at(l0, a0, corner[nc++]);
    if (l0.axis != l1.axis) {
        /* Across: one corner where the two streets meet. */
        corner[nc][0] = l0.axis == 0 ? l0.k * WG_GRID : l1.k * WG_GRID;
        corner[nc][1] = l0.axis == 0 ? l1.k * WG_GRID : l0.k * WG_GRID;
        nc++;
    } else if (l0.k != l1.k) {
        /* Side by side: along a cross street, the one making the way shortest (going on the way the rig
         * faces, and not through the roundabout, where there is a choice). */
        float best = 1e30f, ahead = l0.axis == 0 ? fv : fu;
        int m, bm = 0;
        for (m = -b; m <= b; m++) {
            float c = fabsf(a0 - m * WG_GRID) + fabsf(a1 - m * WG_GRID) + fabsf((float)(l1.k - l0.k)) * WG_GRID;
            if (m == 0 && (l0.k == 0 || l1.k == 0 || (l0.k < 0) != (l1.k < 0)))
                c += 40.0f;                                  /* round the ring */
            if ((m * WG_GRID - a0) * ahead < -1.0f)
                c += 60.0f;                                  /* a turn in the road first */
            else if (ahead != 0 && fabsf(m * WG_GRID - a0) < 1.0f)
                c += 400.0f;                                 /* (at a crossing, turned this way: not straight back) */
            if (c < best) {
                best = c;
                bm = m;
            }
        }
        at(l0, bm * WG_GRID, corner[nc++]);
        at(l1, bm * WG_GRID, corner[nc++]);
    }
    at(l1, a1, corner[nc++]);
    return nc;
}

/* The first way along the corners goes against (fu, fv). */
static int against(const float (*c)[2], int nc, float fu, float fv)
{
    int i;
    for (i = 1; i < nc; i++) {
        float dx = c[i][0] - c[0][0], dy = c[i][1] - c[0][1];
        if (dx * dx + dy * dy > 4.0f)
            return dx * fu + dy * fv < 0;
    }
    return 0;
}

#define FLARE 8.0f              /* metres: a street leaves its line this far out to join the ring a little round */
#define FLARE_A 0.35f           /* radians round the ring where it joins (8.4 m) */

/* Round the ring clockwise from e to x (both on it) into r: never less than 30 degrees round, so an exit beside
 * the entry is all the way round (a U-turn at a roundabout). Flared: joining the ring FLARE_A round from e,
 * leaving it FLARE_A before x (the streets' last FLARE metres are left out: the caller's), so a rig takes it
 * in two easy bends, not one square one; then e and x themselves are left out too. */
static int round_ring(const float *e, const float *x, float (*r)[2], int max)
{
    float ae = atan2f(e[1], e[0]), ax = atan2f(x[1], x[0]), sweep = ae - ax, e2[2], x2[2];
    int n = 0;
    while (sweep < 0)
        sweep += 2 * WG_PI;
    while (sweep >= 2 * WG_PI)
        sweep -= 2 * WG_PI;
    if (max < 4)
        return 0;
    if (sweep >= 0.5f && sweep < 2 * FLARE_A + 0.2f)
        sweep += 2 * WG_PI;                                  /* (too close to flare: all the way round) */
    ae -= FLARE_A;
    ax += FLARE_A;
    e2[0] = WG_RING_R * cosf(ae);
    e2[1] = WG_RING_R * sinf(ae);
    x2[0] = WG_RING_R * cosf(ax);
    x2[1] = WG_RING_R * sinf(ax);
    e = e2;
    x = x2;
    sweep -= 2 * FLARE_A;
    r[n][0] = e[0];
    r[n][1] = e[1];
    n++;
    if (sweep < 0.5f) {
        float m[2];
        m[0] = WG_RING_R * cosf(ae - WG_PI);
        m[1] = WG_RING_R * sinf(ae - WG_PI);
        n += wg_ring_arc(0, 0, e[0], e[1], m[0], m[1], &r[n], max - n - 2);
        r[n][0] = m[0];
        r[n][1] = m[1];
        n++;
        n += wg_ring_arc(0, 0, m[0], m[1], x[0], x[1], &r[n], max - n - 1);
    } else
        n += wg_ring_arc(0, 0, e[0], e[1], x[0], x[1], &r[n], max - n - 1);
    r[n][0] = x[0];
    r[n][1] = x[1];
    n++;
    return n;
}

static float corners_length(const float (*c)[2], int nc)
{
    float m = 0;
    int i;
    for (i = 1; i < nc; i++)
        m += fabsf(c[i][0] - c[i - 1][0]) + fabsf(c[i][1] - c[i - 1][1]);
    return m;
}

#define AT_RING (WG_RING_R + 8.0f)      /* a point this near the middle is at the roundabout (a road's end) */

int town_path(const wg_town *t, float x0, float y0, float h0, float x1, float y1, float (*out)[2], int max,
              float *metres, int *backwards)
{
    static float work[WORK_MAX][2], ring[WORK_MAX][2], world_pts[WORK_MAX][2];
    float u0, v0, u1, v1, a0, a1, corner[8][2], hd = h0 - t->heading, fu = cosf(hd), fv = sinf(hd);
    sline l0, l1;
    int nc = 0, n = 0, nr = 0, i, k, start_ring, end_ring;
    *metres = 0;
    *backwards = 0;
    if (max < 2)
        return 0;
    to_frame(t, x0, y0, &u0, &v0);
    to_frame(t, x1, y1, &u1, &v1);
    start_ring = u0 * u0 + v0 * v0 < AT_RING * AT_RING;
    end_ring = u1 * u1 + v1 * v1 < AT_RING * AT_RING;
    l0 = snap(t, u0, v0, &a0);
    l1 = snap(t, u1, v1, &a1);
    if (start_ring || end_ring) {
        /* From (or to) the roundabout, as a road reaches it: the middle street, of the two, that makes the way
         * shortest, from (or to) the middle; round the ring below. */
        float best = 1e30f, c2[8][2], bb0 = a0, bb1 = a1;
        sline bm0 = l0, bm1 = l1;
        int ax0, ax1;
        for (ax0 = 0; ax0 < 2; ax0++)
            for (ax1 = 0; ax1 < 2; ax1++) {
                sline m0 = l0, m1 = l1;
                float b0 = a0, b1 = a1;
                int m;
                if (start_ring) {
                    m0.axis = ax0;
                    m0.k = 0;
                    b0 = 0;
                } else if (ax0)
                    continue;
                if (end_ring) {
                    m1.axis = ax1;
                    m1.k = 0;
                    b1 = 0;
                } else if (ax1)
                    continue;
                m = corners(t, m0, b0, start_ring ? 0 : fu, start_ring ? 0 : fv, m1, b1, c2);
                if (corners_length((const float (*)[2])c2, m) < best) {
                    best = corners_length((const float (*)[2])c2, m);
                    memcpy(corner, c2, sizeof(float) * 2 * (size_t)m);
                    nc = m;
                    bm0 = m0;
                    bm1 = m1;
                    bb0 = b0;
                    bb1 = b1;
                }
            }
        l0 = bm0;
        l1 = bm1;
        a0 = bb0;
        a1 = bb1;
    } else
        nc = corners(t, l0, a0, fu, fv, l1, a1, corner);
    {
        if (!start_ring && against((const float (*)[2])corner, nc, fu, fv)) {
            /* Not back the way the rig faces: on to the next crossing ahead (not the roundabout, and far
             * enough to turn), then along the cross street towards the stop and on from there; or, the stop
             * behind on this same street, round the block (along the next street back past it, and across to
             * it again). */
            float ahead = l0.axis == 0 ? fv : fu, ext = (float)t->blocks * WG_GRID, c;
            int s = ahead >= 0 ? 1 : -1, b = (int)t->blocks;
            c = s > 0 ? ceilf((a0 + 10.0f) / WG_GRID) * WG_GRID : floorf((a0 - 10.0f) / WG_GRID) * WG_GRID;
            if (fabsf(c) < 1.0f && l0.k == 0)
                c += s * WG_GRID;
            if (fabsf(c) > ext + 0.5f)
                *backwards = 1;
            else if (l1.axis == l0.axis && l1.k == l0.k) {
                int d = l0.k + 1 <= b ? 1 : -1;
                float back = s > 0 ? floorf((a1 - 10.0f) / WG_GRID) * WG_GRID : ceilf((a1 + 10.0f) / WG_GRID) * WG_GRID;
                sline side;
                if (fabsf(back) < 1.0f && l0.k == 0)
                    back -= s * WG_GRID;
                back = fclamp(back, -ext, ext);
                side.axis = l0.axis;
                side.k = l0.k + d;
                nc = 0;
                at(l0, a0, corner[nc++]);
                at(l0, c, corner[nc++]);
                at(side, c, corner[nc++]);
                at(side, back, corner[nc++]);
                at(l0, back, corner[nc++]);
                at(l1, a1, corner[nc++]);
            } else {
                sline x;
                float ca = l0.k * WG_GRID, tu, tv;           /* the crossing, along the cross street */
                x.axis = 1 - l0.axis;
                x.k = (int)floorf(c / WG_GRID + 0.5f);
                tu = x.axis == 0 ? 0 : (u1 > ca ? 1.0f : -1.0f);
                tv = x.axis == 0 ? (v1 > ca ? 1.0f : -1.0f) : 0;
                at(l0, a0, corner[0]);
                nc = 1 + corners(t, x, ca, tu, tv, l1, a1, &corner[1]);
            }
        }
    }
    {
        /* The corners rounded, ROUND m on the streets' middles (not at the roundabout: its ring does that),
         * so a lone tractor keeps to the street round them. */
        static float rounded[64][2];
        int nrd = 0;
        rounded[nrd][0] = corner[0][0];
        rounded[nrd][1] = corner[0][1];
        nrd++;
        for (i = 1; i + 1 < nc && nrd < 48; i++) {
            float ix = corner[i][0] - corner[i - 1][0], iy = corner[i][1] - corner[i - 1][1];
            float ox = corner[i + 1][0] - corner[i][0], oy = corner[i + 1][1] - corner[i][1];
            float li = sqrtf(ix * ix + iy * iy), lo = sqrtf(ox * ox + oy * oy), turn, rad, tlen, cx, cy, a0, side;
            int m;
            if (li < 1.0f || lo < 1.0f || corner[i][0] * corner[i][0] + corner[i][1] * corner[i][1] < AT_RING * AT_RING) {
                rounded[nrd][0] = corner[i][0];
                rounded[nrd][1] = corner[i][1];
                nrd++;
                continue;
            }
            ix /= li;
            iy /= li;
            ox /= lo;
            oy /= lo;
            turn = atan2f(ix * oy - iy * ox, ix * ox + iy * oy);
            if (fabsf(turn) < 0.1f) {
                rounded[nrd][0] = corner[i][0];
                rounded[nrd][1] = corner[i][1];
                nrd++;
                continue;
            }
            tlen = tanf(fabsf(turn) * 0.5f);
            rad = DGK_MIN(ROUND, 0.45f * DGK_MIN(li, lo) / tlen);
            side = turn > 0 ? 1.0f : -1.0f;              /* the centre: left of the way in for a left turn */
            cx = corner[i][0] - ix * rad * tlen - side * iy * rad;
            cy = corner[i][1] - iy * rad * tlen + side * ix * rad;
            a0 = atan2f(corner[i][1] - iy * rad * tlen - cy, corner[i][0] - ix * rad * tlen - cx);
            for (m = 0; m <= 6; m++) {
                float a = a0 + turn * (float)m / 6.0f;
                rounded[nrd][0] = cx + rad * cosf(a);
                rounded[nrd][1] = cy + rad * sinf(a);
                nrd++;
            }
        }
        rounded[nrd][0] = corner[nc - 1][0];
        rounded[nrd][1] = corner[nc - 1][1];
        nrd++;
        for (i = 0; i + 1 < nrd; i++)
            n = fill(work, n, rounded[i], rounded[i + 1]);
        work[n][0] = rounded[nrd - 1][0];
        work[n][1] = rounded[nrd - 1][1];
        n++;
    }
    /* Where the way meets the roundabout's ring: round it clockwise from where it comes in to where it leaves
     * (a road's end at the ring: where it reaches it). */
    for (i = 0; i < n;) {
        if (work[i][0] * work[i][0] + work[i][1] * work[i][1] >= WG_RING_R * WG_RING_R) {
            ring[nr][0] = work[i][0];
            ring[nr][1] = work[i][1];
            nr++;
            i++;
            continue;
        }
        {
            float e[2], x[2], r;
            int j = i;
            while (j + 1 < n && work[j + 1][0] * work[j + 1][0] + work[j + 1][1] * work[j + 1][1] <
                                    WG_RING_R * WG_RING_R)
                j++;
            if (i > 0)
                crossing(work[i], work[i - 1], e);
            else {
                float pu = start_ring ? u0 : work[0][0], pv = start_ring ? v0 : work[0][1];
                r = sqrtf(pu * pu + pv * pv) + 1e-6f;
                e[0] = pu / r * WG_RING_R;
                e[1] = pv / r * WG_RING_R;
            }
            if (j + 1 < n)
                crossing(work[j], work[j + 1], x);
            else {
                float pu = end_ring ? u1 : work[j][0], pv = end_ring ? v1 : work[j][1];
                r = sqrtf(pu * pu + pv * pv) + 1e-6f;
                x[0] = pu / r * WG_RING_R;
                x[1] = pv / r * WG_RING_R;
            }
            /* The flare: the street's last FLARE m before the ring out (the first point kept), and its first
             * FLARE m after it. */
            while (nr > 1 && ring[nr - 1][0] * ring[nr - 1][0] + ring[nr - 1][1] * ring[nr - 1][1] <
                                 (WG_RING_R + FLARE) * (WG_RING_R + FLARE))
                nr--;
            nr += round_ring(e, x, &ring[nr], WORK_MAX - nr);
            i = j + 1;
            while (i + 1 < n && work[i][0] * work[i][0] + work[i][1] * work[i][1] <
                                    (WG_RING_R + FLARE) * (WG_RING_R + FLARE))
                i++;
        }
    }
    /* Every STEP metres, and at each bend; in the world's frame. */
    n = 0;
    {
        float since = 0;
        for (i = 0; i < nr && n < WORK_MAX; i++) {
            int keep = i == 0 || i == nr - 1;
            if (i > 0)
                since += sqrtf((ring[i][0] - ring[i - 1][0]) * (ring[i][0] - ring[i - 1][0]) +
                               (ring[i][1] - ring[i - 1][1]) * (ring[i][1] - ring[i - 1][1]));
            if (!keep && i + 1 < nr && i > 0) {
                float h1 = atan2f(ring[i][1] - ring[i - 1][1], ring[i][0] - ring[i - 1][0]);
                float h2 = atan2f(ring[i + 1][1] - ring[i][1], ring[i + 1][0] - ring[i][0]), d = h2 - h1;
                while (d > WG_PI)
                    d -= 2 * WG_PI;
                while (d < -WG_PI)
                    d += 2 * WG_PI;
                keep = fabsf(d) > TURN && since > 2.5f;     /* (closer, the left lane's offset folds back) */
            }
            if (keep || since >= STEP) {
                from_frame(t, ring[i][0], ring[i][1], &world_pts[n][0], &world_pts[n][1]);
                n++;
                since = 0;
            }
        }
    }
    if (n < 2) {
        out[0][0] = x0;
        out[0][1] = y0;
        out[1][0] = x1;
        out[1][1] = y1;
        n = 2;
    } else {
        if (n > max)
            n = max;
        wg_offset((const float (*)[2])world_pts, n, WG_LANE, 0, out);   /* in the left lane */
        out[n - 1][0] = x1;                                              /* and to the stop itself */
        out[n - 1][1] = y1;
    }
    for (k = 1; k < n; k++)
        *metres += sqrtf((out[k][0] - out[k - 1][0]) * (out[k][0] - out[k - 1][0]) +
                         (out[k][1] - out[k - 1][1]) * (out[k][1] - out[k - 1][1]));
    return n;
}
