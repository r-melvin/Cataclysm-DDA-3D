#pragma once
#ifndef CATA_SRC_R3D_HUD_MATH_H
#define CATA_SRC_R3D_HUD_MATH_H

#include <cstdint>

#include "r3d_math.h"

// Pure maths behind the 3D view's weather, sky and overlay marks: no SDL and no game state, so
// it is unit tested with the camera (tools/r3d_spike/run_pure_tests.sh).
//
// Bearings are map bearings: radians clockwise from north, in [0, 2 pi). Map directions have y
// growing southwards; world directions are those of r3d_math.h (y north).

namespace r3d
{

struct rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

/**
 * Colour of the sky. daylight is the share of full daylight (0 at night, 1 at noon), cloud how
 * overcast it is (0 clear, 1 the darkest storm), lightning a flash this frame.
 */
rgb sky_color( float daylight, float cloud, bool lightning );

/**
 * World direction towards a body in the sky from its azimuth and altitude in radians, using the
 * convention of sun_azimuth_altitude(): azimuth from the south, positive towards the west.
 */
vec3 sky_direction( float azimuth, float altitude );

/**
 * Horizontal world velocity of the wind, in the units of speed, from CDDA's wind direction in
 * degrees: the direction it blows from, 0 north, 90 east.
 */
vec3 wind_velocity( int direction_deg, float speed );

/** A box of falling particles around a point; see particle_position. */
struct particle_field {
    float half_extent = 20.0f; // the box spans [-half_extent, half_extent] on x and y
    float height = 6.0f;       // and [0, height) on z
    float fall_speed = 6.0f;   // units per second, downwards
    vec3 wind;                 // horizontal drift, units per second
};

/**
 * Where particle number index is at time seconds, relative to the field's centre: it starts at
 * a pseudo-random spot, falls and drifts with the wind, and wraps around inside the box, so a
 * fixed number of particles fills it forever. The centre may move without the particles
 * jumping when the caller passes the same offset into world_offset (positions are wrapped in
 * world space, then made relative to it).
 */
vec3 particle_position( uint32_t index, float seconds, const particle_field &field,
                        const vec3 &world_offset = {} );

/**
 * Density of the distance fog for a weather's sight penalty (1 means none; fog is 1.7). The
 * fraction of colour fogged at distance d is 1 - exp( -( d * density )^2 ).
 */
float fog_density( float sight_penalty );
/** That fraction. */
float fog_amount( float distance, float density );

/** Map bearing of a map offset (dx east, dy south); 0 for no offset. */
float map_bearing( float dx, float dy );
/** Map bearing the orbit camera looks along, for camera::yaw. */
float camera_forward_bearing( float yaw );
/** Signed angle from one bearing to another, in (-pi, pi]; positive is clockwise. */
float bearing_delta( float from, float to );

/** Where a mark for something goes on the edge of the view, see place_edge_mark. */
struct edge_mark {
    bool on_screen = false; // the thing itself is inside the inset view: no mark needed
    float x = 0.0f;         // NDC position of the mark (y up)
    float y = 0.0f;
    float angle = 0.0f;     // direction the mark points, radians from NDC +x, counter-clockwise
};

/**
 * Places a mark on the border of the view inset by margin_x / margin_y (NDC units) pointing
 * at something at clip position (clip_x, clip_y, clip_w). Things behind the camera (clip_w <=
 * 0) are mirrored so the mark still points the way to turn.
 */
edge_mark place_edge_mark( float clip_x, float clip_y, float clip_w, float margin_x,
                           float margin_y );

} // namespace r3d

#endif // CATA_SRC_R3D_HUD_MATH_H
