#include "r3d_camera.h"

#include <algorithm>
#include <cmath>

namespace r3d
{

namespace
{
constexpr float rotate_step = 0.2618f; // 15 degrees
constexpr float pitch_step = 0.0873f;  // 5 degrees
constexpr float zoom_factor = 1.1f;
constexpr float near_plane = 0.5f;
constexpr float far_plane = 400.0f;
constexpr float eighth_turn = 0.78539816f; // 45 degrees
} // namespace

bool frustum::intersects( const aabb &box ) const
{
    for( const plane &pl : planes ) {
        // Positive vertex: the box corner furthest along the plane normal.
        const vec3 p{
            pl.n.x >= 0.0f ? box.max.x : box.min.x,
            pl.n.y >= 0.0f ? box.max.y : box.min.y,
            pl.n.z >= 0.0f ? box.max.z : box.min.z };
        if( dot( pl.n, p ) + pl.d < 0.0f ) {
            return false;
        }
    }
    return true;
}

vec3 camera::stretched_target() const
{
    return { target.x, target.y, target.z * z_scale };
}

float camera::fp_yaw() const
{
    return static_cast<float>( heading ) * eighth_turn + glance_yaw;
}

vec3 camera::fp_forward() const
{
    const float yaw_w = fp_yaw();
    const float p = std::clamp( look_pitch + glance_pitch, -max_look_pitch, max_look_pitch );
    const float cp = std::cos( p );
    return { std::sin( yaw_w ) * cp, std::cos( yaw_w ) * cp, std::sin( p ) };
}

float camera::fp_fovy( float aspect ) const
{
    return 2.0f * std::atan( std::tan( fovx * 0.5f ) / std::max( aspect, 0.01f ) );
}

vec3 camera::eye() const
{
    if( first_person() ) {
        return stretched_target() + vec3{ 0.0f, 0.0f, eye_height };
    }
    const float cp = std::cos( pitch );
    return stretched_target() + vec3{ std::sin( yaw ) * cp, -std::cos( yaw ) * cp, std::sin( pitch ) } *
           distance;
}

mat4 camera::view() const
{
    mat4 stretch;
    stretch.at( 2, 2 ) = z_scale;
    if( first_person() ) {
        const vec3 at = eye();
        return look_at( at, at + fp_forward(), { 0.0f, 0.0f, 1.0f } ) * stretch;
    }
    return look_at( eye(), stretched_target(), { 0.0f, 0.0f, 1.0f } ) * stretch;
}

mat4 camera::projection( float aspect ) const
{
    if( first_person() ) {
        // Never orthographic; the near plane is close because the eye is half a tile from the
        // walls of its own tile.
        return perspective( fp_fovy( aspect ), aspect, fp_near_plane, fp_far_plane );
    }
    if( orthographic ) {
        return r3d::orthographic( distance * std::tan( fovy * 0.5f ), aspect, near_plane, far_plane );
    }
    return perspective( fovy, aspect, near_plane, far_plane );
}

mat4 camera::view_projection( float aspect ) const
{
    return projection( aspect ) * view();
}

frustum camera::view_frustum( float aspect ) const
{
    return frustum_from_matrix( view_projection( aspect ) );
}

ray camera::screen_ray( float ndc_x, float ndc_y, float aspect ) const
{
    mat4 inverse;
    if( !invert( view_projection( aspect ), inverse ) ) {
        return { target, { 0.0f, 0.0f, -1.0f } };
    }
    // Depth range is [0, 1]: z = 0 is the near plane, z = 1 the far plane.
    const vec3 near_pt = transform_point( inverse, { ndc_x, ndc_y, 0.0f } );
    const vec3 far_pt = transform_point( inverse, { ndc_x, ndc_y, 1.0f } );
    // Both points come back in world space (the stretch is part of the matrix).
    return { near_pt, normalize( far_pt - near_pt ) };
}

int camera::forward_eighths() const
{
    if( first_person() ) {
        return ( heading % 8 + 8 ) % 8;
    }
    // Forward in tile space (y south) is away from the eye, flattened.
    const vec3 eye_offset = eye() - stretched_target();
    const float fx = -eye_offset.x;
    const float fy = eye_offset.y;
    if( fx == 0.0f && fy == 0.0f ) {
        return 0;
    }
    constexpr float eighth = 0.78539816f;
    const int turns = static_cast<int>( std::lround( std::atan2( fx, -fy ) / eighth ) );
    return ( turns % 8 + 8 ) % 8;
}

vec3 camera::ground_right() const
{
    if( first_person() ) {
        const float yaw_w = fp_yaw();
        return { std::cos( yaw_w ), -std::sin( yaw_w ), 0.0f };
    }
    return { std::cos( yaw ), std::sin( yaw ), 0.0f };
}

void camera::rotate_steps( int steps )
{
    if( first_person() ) {
        heading = ( ( heading + steps ) % 8 + 8 ) % 8;
        return;
    }
    yaw += rotate_step * static_cast<float>( steps );
    constexpr float two_pi = 6.2831853f;
    yaw = std::fmod( yaw, two_pi );
}

void camera::pitch_steps( int steps )
{
    if( first_person() ) {
        look_pitch = std::clamp( look_pitch + look_pitch_step * static_cast<float>( steps ),
                                 -max_look_pitch, max_look_pitch );
        return;
    }
    pitch = std::clamp( pitch + pitch_step * static_cast<float>( steps ), min_pitch, max_pitch );
}

void camera::zoom_steps( int steps )
{
    if( first_person() ) {
        return;
    }
    // Positive steps move the camera closer.
    distance = std::clamp( distance * std::pow( 1.0f / zoom_factor, static_cast<float>( steps ) ),
                           min_distance, max_distance );
}

frustum frustum_from_matrix( const mat4 &m )
{
    // Gribb/Hartmann extraction, adapted for a [0, 1] depth range: the near plane is row 2 alone
    // rather than row 3 + row 2.
    auto row = [&m]( int r ) {
        return std::array<float, 4> { m.at( r, 0 ), m.at( r, 1 ), m.at( r, 2 ), m.at( r, 3 ) };
    };
    const auto r0 = row( 0 );
    const auto r1 = row( 1 );
    const auto r2 = row( 2 );
    const auto r3 = row( 3 );
    auto make = [&]( const std::array<float, 4> &v ) {
        frustum::plane pl;
        const vec3 n{ v[0], v[1], v[2] };
        const float len = length( n );
        const float inv = len > 0.0f ? 1.0f / len : 1.0f;
        pl.n = n * inv;
        pl.d = v[3] * inv;
        return pl;
    };
    auto add = [&]( const std::array<float, 4> &a, const std::array<float, 4> &b ) {
        return std::array<float, 4> { a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3] };
    };
    auto sub = [&]( const std::array<float, 4> &a, const std::array<float, 4> &b ) {
        return std::array<float, 4> { a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3] };
    };
    frustum f;
    f.planes[0] = make( add( r3, r0 ) ); // left
    f.planes[1] = make( sub( r3, r0 ) ); // right
    f.planes[2] = make( add( r3, r1 ) ); // bottom
    f.planes[3] = make( sub( r3, r1 ) ); // top
    f.planes[4] = make( r2 );            // near
    f.planes[5] = make( sub( r3, r2 ) ); // far
    return f;
}

} // namespace r3d
