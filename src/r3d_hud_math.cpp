#include "r3d_hud_math.h"

#include <algorithm>
#include <cmath>

namespace r3d
{

namespace
{
constexpr float pi = 3.14159265f;
constexpr float two_pi = 6.2831853f;

float lerp( float a, float b, float t )
{
    return a + ( b - a ) * t;
}

float wrap_bearing( float b )
{
    b = std::fmod( b, two_pi );
    return b < 0.0f ? b + two_pi : b;
}

// Repeatable pseudo-random number in [0, 1) for a particle and a channel.
float hash01( uint32_t index, uint32_t channel )
{
    uint32_t h = index * 0x9e3779b1u ^ ( channel + 1u ) * 0x85ebca77u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return static_cast<float>( h >> 8 ) / 16777216.0f;
}

// x wrapped into [lo, lo + span).
float wrap( float x, float lo, float span )
{
    const float t = std::fmod( x - lo, span );
    return lo + ( t < 0.0f ? t + span : t );
}
} // namespace

rgb sky_color( float daylight, float cloud, bool lightning )
{
    daylight = std::clamp( daylight, 0.0f, 1.0f );
    cloud = std::clamp( cloud, 0.0f, 1.0f );
    if( lightning ) {
        return { 0.85f, 0.87f, 0.95f };
    }
    // Night is a deep blue, not black, so the sky still reads as sky around the map.
    const rgb night{ 0.03f, 0.04f, 0.09f };
    const rgb day{ 0.42f, 0.62f, 0.86f };
    // Twilight warms the colour while the sun is low.
    const float dusk = std::clamp( 1.0f - std::fabs( daylight - 0.25f ) / 0.2f, 0.0f, 1.0f );
    rgb c{ lerp( night.r, day.r, daylight ), lerp( night.g, day.g, daylight ),
           lerp( night.b, day.b, daylight ) };
    c.r = lerp( c.r, 0.62f, dusk * 0.45f );
    c.g = lerp( c.g, 0.42f, dusk * 0.3f );
    // Cloud turns the colour grey and darker.
    const float grey = ( c.r + c.g + c.b ) / 3.0f * ( 1.0f - 0.55f * cloud );
    return { lerp( c.r, grey, cloud ), lerp( c.g, grey, cloud ), lerp( c.b, grey, cloud ) };
}

vec3 sky_direction( float azimuth, float altitude )
{
    // Azimuth 0 is south, positive towards the west (see sunlight_angle in calendar.cpp).
    const float ca = std::cos( altitude );
    return { -std::sin( azimuth ) * ca, -std::cos( azimuth ) * ca, std::sin( altitude ) };
}

vec3 wind_velocity( int direction_deg, float speed )
{
    // The wind comes from direction_deg, so it blows the opposite way.
    const float a = static_cast<float>( direction_deg ) * pi / 180.0f;
    return { -std::sin( a ) * speed, -std::cos( a ) * speed, 0.0f };
}

vec3 particle_position( uint32_t index, float seconds, const particle_field &field,
                        const vec3 &world_offset )
{
    const float span = std::max( field.half_extent * 2.0f, 0.001f );
    const float height = std::max( field.height, 0.001f );
    // Each particle falls at its own speed (+-25%) so they do not move as one sheet.
    const float speed = 0.75f + 0.5f * hash01( index, 3 );
    const float x = hash01( index, 0 ) * span + field.wind.x * speed * seconds;
    const float y = hash01( index, 1 ) * span + field.wind.y * speed * seconds;
    const float z = hash01( index, 2 ) * height - field.fall_speed * speed * seconds;
    return { wrap( x, world_offset.x - field.half_extent, span ) - world_offset.x,
             wrap( y, world_offset.y - field.half_extent, span ) - world_offset.y,
             wrap( z, 0.0f, height ) };
}

float fog_density( float sight_penalty )
{
    return std::max( sight_penalty - 1.0f, 0.0f ) * 0.06f;
}

float fog_amount( float distance, float density )
{
    const float d = distance * density;
    return 1.0f - std::exp( -d * d );
}

float map_bearing( float dx, float dy )
{
    if( dx == 0.0f && dy == 0.0f ) {
        return 0.0f;
    }
    return wrap_bearing( std::atan2( dx, -dy ) );
}

float camera_forward_bearing( float yaw )
{
    // camera::eye() sits at world ( sin yaw, -cos yaw ) from the target and looks back at it.
    return wrap_bearing( -yaw );
}

float bearing_delta( float from, float to )
{
    float d = std::fmod( to - from, two_pi );
    if( d <= -pi ) {
        d += two_pi;
    } else if( d > pi ) {
        d -= two_pi;
    }
    return d;
}

edge_mark place_edge_mark( float clip_x, float clip_y, float clip_w, float margin_x,
                           float margin_y )
{
    edge_mark m;
    float x = 0.0f;
    float y = 0.0f;
    if( clip_w > 1e-6f ) {
        x = clip_x / clip_w;
        y = clip_y / clip_w;
    } else {
        // Behind the camera the projection flips; mirror back so the mark points the way to
        // turn. Something straight behind is shown at the bottom.
        x = -clip_x;
        y = -clip_y;
        if( std::fabs( x ) < 1e-6f && std::fabs( y ) < 1e-6f ) {
            y = -1.0f;
        }
    }
    const float lim_x = std::max( 1.0f - margin_x, 0.01f );
    const float lim_y = std::max( 1.0f - margin_y, 0.01f );
    if( clip_w > 1e-6f && std::fabs( x ) <= lim_x && std::fabs( y ) <= lim_y ) {
        m.on_screen = true;
        m.x = x;
        m.y = y;
        return m;
    }
    const float tx = std::fabs( x ) > 1e-6f ? lim_x / std::fabs( x ) : 1e9f;
    const float ty = std::fabs( y ) > 1e-6f ? lim_y / std::fabs( y ) : 1e9f;
    const float t = std::min( tx, ty );
    m.x = x * t;
    m.y = y * t;
    m.angle = std::atan2( y, x );
    return m;
}

} // namespace r3d
