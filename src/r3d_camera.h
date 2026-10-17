#pragma once
#ifndef CATA_SRC_R3D_CAMERA_H
#define CATA_SRC_R3D_CAMERA_H

#include <array>
#include <cstdint>

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

enum class camera_mode : uint8_t {
    orbit,
    first_person
};

/**
 * Orbit camera around a target point. yaw 0 puts the camera south of the target looking north;
 * increasing yaw swings it clockwise seen from above. pitch is the elevation above the horizon.
 *
 * In first-person mode the same struct is an eye standing on the target (the point on the floor
 * under the viewer, in world space), facing one of the eight map directions given by heading.
 * yaw, pitch, distance and fovy are then unused; the fields below take their place.
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

    camera_mode mode = camera_mode::orbit;
    // First person only.
    static constexpr float fp_near_plane = 0.05f;
    static constexpr float fp_far_plane = 200.0f;
    static constexpr float max_look_pitch = 1.2f; // ~70 degrees
    static constexpr float look_pitch_step = 0.2618f; // 15 degrees
    int heading = 0;           // 1/8 turns clockwise from north, 0..7 (map convention, y south)
    float look_pitch = 0.0f;   // radians, positive looks up
    float eye_height = 0.8f;   // above the target, in stretched space (see z_scale)
    float glance_yaw = 0.0f;   // mouse glance, display only; snaps back to 0
    float glance_pitch = 0.0f;
    float tween_yaw = 0.0f;    // the turn still to be shown while a turn is eased, display only
    float fovx = 1.5708f;      // horizontal field of view, 90 degrees

    bool first_person() const {
        return mode == camera_mode::first_person;
    }
    /** First person: yaw of the view in world space (0 looks north, positive swings east). */
    float fp_yaw() const;
    /** First person: unit view direction in stretched space. */
    vec3 fp_forward() const;
    /** Vertical field of view that gives fovx at the aspect ratio (width / height). */
    float fp_fovy( float aspect ) const;

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

    /**
     * How many 1/8 turns clockwise (on the map, y south) the camera's forward direction is from
     * north, rounded: 0 when looking north, 2 east, 6 west. Turning a map direction by this
     * makes "up" walk away from the camera.
     */
    int forward_eighths() const;

    /**
     * Orbit by steps of 15 degrees / tilt by steps of 5 degrees / scale distance by 10%. In first
     * person a rotate step is one eighth turn clockwise (no tween here, no game time), a pitch
     * step looks up or down by 15 degrees, and zoom does nothing.
     */
    void rotate_steps( int steps );
    void pitch_steps( int steps );
    void zoom_steps( int steps );
};

/**
 * The heading (1/8 turns clockwise from north, 0..7) that looks from a tile towards one dx tiles
 * east and dy tiles south of it (map convention, y south). 0 for the same tile.
 */
int eighths_towards( int dx, int dy );

/** Extracts the six clip planes from a view-projection matrix (depth range [0, 1]). */
frustum frustum_from_matrix( const mat4 &view_proj );

} // namespace r3d

#endif // CATA_SRC_R3D_CAMERA_H
