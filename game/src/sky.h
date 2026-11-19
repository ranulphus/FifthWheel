/* sky.h - the sky and the haze, when the camera comes down low enough at
 * speed to see far (MEDIUM and HIGH: detail.h's pitch_min): a gradient from
 * the zenith's blue to the horizon's pale blue above the horizon line, and
 * linear fog in the horizon's colour over the last half of the view, so the
 * world ends in haze rather than at an edge. LOW's camera never comes that
 * low (no fog on the G200, where DOS-GL's fog costs most). */
#ifndef FW_SKY_H
#define FW_SKY_H

#define SKY_HORIZON 0xCFE6F5u        /* the horizon, the haze and the clear colour */

/* Before the world: the sky above the horizon, if the horizon is in the
 * picture (view_pitch from camera_apply). */
void sky_draw(float view_pitch);
/* How far from the eye (eye_height above the ground) the ground at the
 * picture's top edge is, at most `far`: as far as the world need be drawn. */
float sky_seen(float view_pitch, float eye_height, float far);
/* Then: fog on for the world and the lorries if the ground the camera sees
 * reaches towards the far plane; 1 if it is. */
int  sky_fog(float view_pitch, float eye_height, float far);

#endif
