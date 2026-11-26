/* meshlit.c - the meshes lit by GL on a card whose transform engine lights (mesh.h's mb_engine): the game's
 * only, as it needs GL (the host tools share mesh.c). */
#include "mesh.h"
#include <GL/gl.h>

static int engine_light;

void mb_engine(int on)
{
    engine_light = on;
}

void mb_light_begin(void)
{
    static const GLfloat sun[4] = { -0.45f, 0.75f, -0.48f, 0.0f }, ambient[4] = { 0.58f, 0.58f, 0.58f, 1.0f };
    static const GLfloat diffuse[4] = { 0.42f, 0.42f, 0.42f, 1.0f }, gloss[4] = { MB_GLOSS, MB_GLOSS, MB_GLOSS, 1.0f };
    static const GLfloat none[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (!engine_light)
        return;
    glLightfv(GL_LIGHT0, GL_POSITION, sun);          /* (a direction: the camera's matrix carries it to the eye) */
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, gloss);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, none);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, (float)(1 << MB_GLOSS_POWER));
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
}

void mb_light_end(void)
{
    if (!engine_light)
        return;
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
}

void mb_draw(const dgk_mesh *m, builder *b, float yaw)
{
    int i;
    if (!engine_light || !b->nrm) {
        mb_relight(b, yaw);
        b->unlit = 0;
        dgk_gfx_draw_mesh(m);
        return;
    }
    if (!b->unlit) {                                 /* the colours as made: GL lights them */
        for (i = 0; i < b->nverts; i++) {
            b->rgba[i * 4 + 0] = (uint8_t)(b->base[i] >> 16);
            b->rgba[i * 4 + 1] = (uint8_t)(b->base[i] >> 8);
            b->rgba[i * 4 + 2] = (uint8_t)b->base[i];
            b->rgba[i * 4 + 3] = 255;
        }
        b->unlit = 1;
    }
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, 0, b->nrm);
    dgk_gfx_draw_mesh(m);
    glDisableClientState(GL_NORMAL_ARRAY);
}

