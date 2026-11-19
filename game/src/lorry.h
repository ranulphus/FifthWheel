/* lorry.h - the lorry's meshes: the tractor about its rear axle, the trailer
 * about its kingpin, both facing +x (east) in GL coordinates. */
#ifndef FW_LORRY_H
#define FW_LORRY_H

#include "mesh.h"
#include "vehicle.h"

typedef struct lorry_meshes {
    dgk_mesh chassis, cab, trailer[TRAILER_TYPES];
    builder chassis_b, cab_b, trailer_b[TRAILER_TYPES];   /* their normals: lit again as they turn */
    int quality;                                          /* detail.h's models: 0 boxes .. 2 rounder */
} lorry_meshes;

/* How the cab sits on its springs (looks only): degrees of roll (to the
 * left) and pitch (nose up), metres of lift. */
typedef struct cab_sway {
    float roll, pitch, lift;
} cab_sway;

enum { DECAL_NONE, DECAL_STRIPES, DECAL_FLAMES, DECAL_LIGHTNING, DECAL_STARS };
enum { CAB_STANDARD, CAB_BIG };

/* The trailers, and the tractor as it starts, `quality` round (detail.h's
 * models: 0 boxes, 1 rounded, 2 rounder); again when the detail changes. */
void lorry_build(lorry_meshes *m, int quality);
/* The tractor's paint (0: rainbow), decal and cab (rebuilt; cheap). */
void lorry_style(lorry_meshes *m, uint32_t paint, int decal, int cab);
/* At the rig's pose, on the ground (w NULL: flat at height 0); the cab
 * swayed (NULL: still). */
void lorry_draw(lorry_meshes *m, const rig *r, const world *w, const cab_sway *sway);
/* A trailer standing on its legs, kingpin at (kx, ky). */
void lorry_draw_trailer(lorry_meshes *m, int type, float kx, float ky, float heading, const world *w);
/* Soft contact shadows under the rig and standing trailers, a little to the
 * south-east (the sun is in the north-west, as the world's shadows have it).
 * Drawn after the world and before the lorries, over whatever is there (the
 * lorries then draw over them): a road can stand well above the terrain the
 * lorry rides, and would hide a shadow laid on that (w NULL: flat at 0). */
void lorry_shadows_begin(void);
void lorry_shadow_rig(const rig *r, const world *w);
void lorry_shadow_trailer(float kx, float ky, float heading, const world *w);
void lorry_shadows_end(void);

#endif
