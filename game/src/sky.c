/* sky.c - the sky and the haze (see sky.h). */
#include "sky.h"
#include "camera.h"
#include "dgk/gfx.h"
#include <GL/gl.h>
#include <math.h>

#define SKY_ZENITH 0x4F9CE8u
#define DEG (3.14159265f / 180.0f)

static void colour(uint32_t rgb)
{
    glColor4ub((GLubyte)(rgb >> 16), (GLubyte)(rgb >> 8), (GLubyte)rgb, 255);
}

void sky_draw(float view_pitch)
{
    /* The horizon's height in the overlay: above the middle by the view's
     * pitch (the picture is CAMERA_FOVY tall). */
    float y = 240.0f - 240.0f * tanf(view_pitch) / tanf(CAMERA_FOVY * 0.5f * DEG);
    if (y <= 0)
        return;
    dgk_gfx_overlay_begin();
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    colour(SKY_ZENITH);
    glVertex2f(0, y - 320.0f);
    glVertex2f(640, y - 320.0f);
    colour(SKY_HORIZON);
    glVertex2f(640, y + 1.0f);
    glVertex2f(0, y + 1.0f);
    glEnd();
    dgk_gfx_overlay_end();
}

float sky_seen(float view_pitch, float eye_height, float far)
{
    float top = view_pitch - CAMERA_FOVY * 0.5f * DEG, seen = top > 0.001f ? eye_height / tanf(top) : far;
    return seen < far ? seen : far;
}

int sky_fog(float view_pitch, float eye_height, float far)
{
    GLfloat c[4];
    if (sky_seen(view_pitch, eye_height, far) < far * 0.7f)
        return 0;
    c[0] = ((SKY_HORIZON >> 16) & 255) / 255.0f;
    c[1] = ((SKY_HORIZON >> 8) & 255) / 255.0f;
    c[2] = (SKY_HORIZON & 255) / 255.0f;
    c[3] = 1.0f;
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, c);
    glFogf(GL_FOG_START, far * 0.5f);
    glFogf(GL_FOG_END, far * 0.95f);
    glEnable(GL_FOG);
    return 1;
}
