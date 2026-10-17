#pragma once
#ifndef CATA_SRC_R3D_DDA_H
#define CATA_SRC_R3D_DDA_H

#include <functional>

#include "r3d_camera.h"
#include "r3d_math.h"

namespace r3d
{

/** Which face of a cell column a ray entered through; none when the ray starts inside it. */
enum class hit_face : int {
    none = -1,
    west,   // -x
    east,   // +x
    north,  // -y in tile space
    south,  // +y in tile space
    bottom, // -z
    top     // +z
};

struct voxel_hit {
    bool hit = false;
    int x = 0;
    int y = 0;
    int z = 0;
    float t = 0.0f; // distance along the ray in units of the (unit) direction
    hit_face face = hit_face::none;
};

/** Height in [0, 1] of the solid column filling the bottom of cell (x, y, z); 0 means empty. */
using column_height_fn = std::function<float( int x, int y, int z )>;

/**
 * Walks a ray through unit cells (Amanatides & Woo) and returns the first cell whose column box
 * [x, x+1] x [y, y+1] x [z, z+height] the ray touches, within max_t. Works in tile space: cell
 * (x, y, z) covers [x, x+1] on each axis, and the ray must be given in that space too.
 */
voxel_hit raycast_columns( const vec3 &origin, const vec3 &dir, float max_t,
                           const column_height_fn &height );

/**
 * Whether the straight line from one point to another (tile space) reaches the cell holding the
 * second point without first touching another cell's column: what lies there can be seen from
 * the first point unless something nearer stands in the way.
 */
bool clear_line( const vec3 &from, const vec3 &to, const column_height_fn &height );

/** Slow reference: tests every cell in the box [lo, hi] and keeps the nearest hit. For tests. */
voxel_hit raycast_columns_reference( const vec3 &origin, const vec3 &dir, float max_t,
                                     const column_height_fn &height,
                                     int lo_x, int lo_y, int lo_z, int hi_x, int hi_y, int hi_z );

/**
 * The cell under a point of the viewport (ndc_x/ndc_y in [-1, 1], y up). The camera works in world
 * space and the cells in tile space (see tile_to_world). When the ray hits no column within max_t
 * it is intersected with the plane z = floor_z instead, so pointing at empty space still picks a
 * cell there (with face top); hit is false only when the ray never reaches that plane either.
 */
voxel_hit pick_cell( const camera &cam, float ndc_x, float ndc_y, float aspect, float max_t,
                     const column_height_fn &height, float floor_z );

} // namespace r3d

#endif // CATA_SRC_R3D_DDA_H
