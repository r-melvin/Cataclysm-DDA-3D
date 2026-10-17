#pragma once
#ifndef CATA_SRC_R3D_CAMERA_H
#define CATA_SRC_R3D_CAMERA_H

#include <array>

#include "r3d_math.h"

namespace r3d
{

/** World <-> tile conversions. A tile (tx, ty, tz) occupies [tx, tx+1] x [ty, ty+1] x [tz, tz+1]
 *  in tile space; world space flips y so that it is right-handed (see r3d_math.h). */
inline vec3 tile_to_world( const vec3 &t )
{
    return { t.x, -t.y, t.z };
}
inline vec3 world_to_tile( const vec3 &w )
{
    return { w.x, -w.y, w.z };
}

struct ray {
    vec3 origin;
    vec3 dir; // normalized
};

/** Axis-aligned box in world space. */
struct aabb {
    vec3 min;
    vec3 max;
};

/** Six frustum planes; a point p is inside when dot(n, p) + d >= 0 for every plane. */
struct frustum {
    struct plane {
        vec3 n;
        float d = 0.0f;
    };
    std::array<plane, 6> planes;

    /** False only when the box is entirely outside one plane (conservative). */
    bool intersects( const aabb &box ) const;
};

/**
 * Orbit camera around a target point. yaw 0 puts the camera south of the target looking north;
 * increasing yaw swings it clockwise seen from above. pitch is the elevation above the horizon.
 */
struct camera {
    static constexpr float min_pitch = 0.17f;  // ~10 degrees
    static constexpr float max_pitch = 1.553f; // ~89 degrees, avoids the look_at singularity
    static constexpr float min_distance = 4.0f;
    static constexpr float max_distance = 120.0f;

    vec3 target;               // world space, normally the centre of the viewed tile
    // Height of one z-level in tile widths. World space counts z in levels (a wall is 1 high);
    // the view stretches it by this so storeys look taller than a tile is wide. Everything the
    // camera returns in "stretched" space below has z multiplied by it; rays and matrices map
    // straight to and from (unstretched) world space.
    float z_scale = 1.0f;
    float yaw = 0.0f;
    float pitch = 0.96f;       // ~55 degrees
    float distance = 24.0f;
    float fovy = 0.785f;       // 45 degrees
    bool orthographic = false;

    /** Camera position in stretched space (see z_scale). */
    vec3 eye() const;
    /** target in stretched space. */
    vec3 stretched_target() const;
    mat4 view() const;
    mat4 projection( float aspect ) const;
    mat4 view_projection( float aspect ) const;
    frustum view_frustum( float aspect ) const;

    /** Ray through a point of the viewport; ndc_x/ndc_y are in [-1, 1] with y up. World space. */
    ray screen_ray( float ndc_x, float ndc_y, float aspect ) const;

    /** Unit vector pointing right on screen, flattened onto the ground plane. */
    vec3 ground_right() const;

    /** Orbit by steps of 15 degrees / tilt by steps of 5 degrees / scale distance by 10%. */
    void rotate_steps( int steps );
    void pitch_steps( int steps );
    void zoom_steps( int steps );
};

/** Extracts the six clip planes from a view-projection matrix (depth range [0, 1]). */
frustum frustum_from_matrix( const mat4 &view_proj );

} // namespace r3d

#endif // CATA_SRC_R3D_CAMERA_H
