/* lorry.h - the lorry's meshes: the tractor about its rear axle, the trailer
 * about its kingpin, both facing +x (east) in GL coordinates. */
#ifndef FW_LORRY_H
#define FW_LORRY_H

#include "mesh.h"
#include "vehicle.h"

typedef struct lorry_meshes {
    dgk_mesh tractor, trailer;
} lorry_meshes;

void lorry_build(lorry_meshes *m);
void lorry_draw(const lorry_meshes *m, const rig *r);   /* at the rig's pose */

#endif
