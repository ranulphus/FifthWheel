/* gfx.c - camera matrices, meshes from arrays, the 2D overlay. */
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <math.h>

uint32_t dgk_gfx_tris;

void dgk_gfx_perspective(float fovy_deg, float aspect, float znear, float zfar)
{
    float f = znear * tanf(fovy_deg * 3.14159265f / 360.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-f * aspect, f * aspect, -f, f, znear, zfar);
    glMatrixMode(GL_MODELVIEW);
}

static dgk_v3 norm(dgk_v3 v)
{
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    dgk_v3 r = { v.x / l, v.y / l, v.z / l };
    return r;
}

static dgk_v3 cross(dgk_v3 a, dgk_v3 b)
{
    dgk_v3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}

void dgk_gfx_look_at(dgk_v3 eye, dgk_v3 at, dgk_v3 up)
{
    dgk_v3 f = { at.x - eye.x, at.y - eye.y, at.z - eye.z }, s, u;
    GLfloat m[16];
    f = norm(f);
    s = norm(cross(f, up));
    u = cross(s, f);
    m[0] = s.x; m[4] = s.y; m[8] = s.z;   m[12] = 0;
    m[1] = u.x; m[5] = u.y; m[9] = u.z;   m[13] = 0;
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = 0;
    m[3] = 0;   m[7] = 0;   m[11] = 0;    m[15] = 1;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

void dgk_gfx_draw_mesh(const dgk_mesh *m)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, m->pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, m->rgba);
    glDrawElements(GL_TRIANGLES, m->ntris * 3, GL_UNSIGNED_SHORT, m->idx);
    dgk_gfx_tris += (uint32_t)m->ntris;
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void dgk_gfx_overlay_begin(void)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 640, 480, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
}

void dgk_gfx_overlay_end(void)
{
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
}
