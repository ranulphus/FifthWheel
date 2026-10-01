/* lorry.h - the lorry's meshes: the tractor about its rear axle, the trailer
 * about its kingpin, both facing +x (east) in GL coordinates. */
#ifndef FW_LORRY_H
#define FW_LORRY_H

#include "mesh.h"
#include "vehicle.h"

typedef struct lorry_meshes {
    dgk_mesh chassis, cab, trailer[TRAILER_TYPES];
} lorry_meshes;

/* How the cab sits on its springs (looks only): degrees of roll (to the
 * left) and pitch (nose up), metres of lift. */
typedef struct cab_sway {
    float roll, pitch, lift;
} cab_sway;

enum { DECAL_NONE, DECAL_STRIPES, DECAL_FLAMES, DECAL_LIGHTNING, DECAL_STARS };
enum { CAB_STANDARD, CAB_BIG };

void lorry_build(lorry_meshes *m);           /* the trailers, and the tractor as it starts */
/* The tractor's paint (0: rainbow), decal and cab (rebuilt; cheap). */
void lorry_style(lorry_meshes *m, uint32_t paint, int decal, int cab);
/* At the rig's pose, on the ground (w NULL: flat at height 0); the cab
 * swayed (NULL: still). */
void lorry_draw(const lorry_meshes *m, const rig *r, const world *w, const cab_sway *sway);
/* A trailer standing on its legs, kingpin at (kx, ky). */
void lorry_draw_trailer(const lorry_meshes *m, int type, float kx, float ky, float heading, const world *w);

#endif
