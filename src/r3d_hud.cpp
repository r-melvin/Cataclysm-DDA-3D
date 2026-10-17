#include "r3d_hud.h"

#include <algorithm>
#include <cmath>

namespace r3d
{

namespace
{
constexpr float pi = 3.14159265f;
constexpr float eighth_turn = pi * 0.25f;
// A hair over 135 degrees: the rear diagonals are side alerts, not "behind" (see edge_alert_side).
constexpr float behind_from = pi * 0.75f + 1e-3f;
} // namespace

float map_bearing( float dx, float dy )
{
    if( dx == 0.0f && dy == 0.0f ) {
        return 0.0f;
    }
    // North is -y on the map, so the bearing is the angle of (dx, -dy) from the +y axis.
    return std::atan2( dx, -dy );
}

float wrap_angle( float radians )
{
    float a = std::fmod( radians, 2.0f * pi );
    if( a > pi ) {
        a -= 2.0f * pi;
    } else if( a <= -pi ) {
        a += 2.0f * pi;
    }
    return a;
}

float relative_bearing( float dx, float dy, float view_yaw )
{
    return wrap_angle( map_bearing( dx, dy ) - view_yaw );
}

float heading_bearing( int heading )
{
    return wrap_angle( static_cast<float>( ( heading % 8 + 8 ) % 8 ) * eighth_turn );
}

float compass_offset( float rel, float strip_width, float span )
{
    const float half = span * 0.5f;
    return std::clamp( rel, -half, half ) / half * strip_width * 0.5f;
}

bool on_compass( float rel, float span )
{
    return std::abs( rel ) <= span * 0.5f + 1e-4f;
}

edge_side edge_alert_side( float rel, float fovx )
{
    const float a = std::abs( rel );
    // Exactly on the picture's edge (a diagonal neighbour at a 90 degree view) half the sprite is
    // in the picture: that counts as seen, whatever the rounding of the bearing.
    if( a <= fovx * 0.5f + 1e-3f ) {
        return edge_side::none;
    }
    if( a >= behind_from ) {
        return edge_side::behind;
    }
    return rel < 0.0f ? edge_side::left : edge_side::right;
}

float minimap_turn( int heading )
{
    return -heading_bearing( heading );
}

void turn_on_screen( float &x, float &y, float cx, float cy, float radians )
{
    // With y pointing down, the usual rotation matrix turns clockwise as seen on screen.
    const float c = std::cos( radians );
    const float s = std::sin( radians );
    const float dx = x - cx;
    const float dy = y - cy;
    x = cx + dx * c - dy * s;
    y = cy + dx * s + dy * c;
}

} // namespace r3d
