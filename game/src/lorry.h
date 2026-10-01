/* lorry.h - the lorry's meshes: the tractor about its rear axle, the trailer
 * about its kingpin, both facing +x (east) in GL coordinates. */
#ifndef FW_LORRY_H
#define FW_LORRY_H

#include "mesh.h"
#include "vehicle.h"

typedef struct lorry_meshes {
    dgk_mesh tractor, trailer[TRAILER_TYPES];
} lorry_meshes;

void lorry_build(lorry_meshes *m);
void lorry_draw(const lorry_meshes *m, const rig *r, const world *w);   /* at the rig's pose, on the ground */
/* A trailer standing on its legs, kingpin at (kx, ky). */
void lorry_draw_trailer(const lorry_meshes *m, int type, float kx, float ky, float heading, const world *w);

#endif
