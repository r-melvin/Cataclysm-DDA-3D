#if defined(TILES)

#include "r3d_sky.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "avatar.h"
#include "calendar.h"
#include "messages.h"
#include "translations.h"
#include "r3d_gpu.h"
#include "weather.h"
#include "weather_type.h"

namespace r3d
{

namespace
{

constexpr int cell = 32;

struct painter {
    SDL_Surface *surface;

    void put( int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
        if( x < 0 || y < 0 || x >= surface->w || y >= surface->h ) {
            return;
        }
        uint8_t *const px = static_cast<uint8_t *>( surface->pixels ) + y * surface->pitch + x * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    }
};

// Unit disc coordinates of a pixel of a cell: x right, y up, 1 at the rim (with a margin).
void disc_coords( int px, int py, float &x, float &y )
{
    const float half = cell * 0.5f;
    x = ( static_cast<float>( px ) + 0.5f - half ) / ( half - 2.0f );
    y = ( half - static_cast<float>( py ) - 0.5f ) / ( half - 2.0f );
}

void paint_sun( painter &p, int ox )
{
    for( int py = 0; py < cell; ++py ) {
        for( int px = 0; px < cell; ++px ) {
            float x = 0.0f;
            float y = 0.0f;
            disc_coords( px, py, x, y );
            const float r2 = x * x + y * y;
            if( r2 <= 1.0f ) {
                // Brighter in the middle, warm at the rim.
                const uint8_t g = static_cast<uint8_t>( 255.0f - 60.0f * r2 );
                const uint8_t b = static_cast<uint8_t>( 200.0f - 120.0f * r2 );
                p.put( ox + px, py, 255, g, b );
            }
        }
    }
}

// Phase 0 is the new moon, 4 the full moon; 1-3 wax (lit on the right), 5-7 wane.
void paint_moon( painter &p, int ox, int phase )
{
    constexpr float quarter = 0.78539816f;
    const float angle = static_cast<float>( phase ) * quarter;
    const bool waxing = phase <= 4;
    for( int py = 0; py < cell; ++py ) {
        for( int px = 0; px < cell; ++px ) {
            float x = 0.0f;
            float y = 0.0f;
            disc_coords( px, py, x, y );
            const float r2 = x * x + y * y;
            if( r2 > 1.0f ) {
                continue;
            }
            // The terminator is an ellipse: x = cos( angle ) * sqrt( 1 - y^2 ).
            const float edge = std::cos( angle ) * std::sqrt( std::max( 0.0f, 1.0f - y * y ) );
            const bool lit = waxing ? x >= edge : -x >= std::cos( 6.2831853f - angle ) *
                             std::sqrt( std::max( 0.0f, 1.0f - y * y ) );
            if( lit ) {
                const uint8_t v = static_cast<uint8_t>( 235.0f - 30.0f * r2 );
                p.put( ox + px, py, v, v, static_cast<uint8_t>( v * 0.92f ) );
            } else {
                // The dark part stays faintly visible, so a new moon is still a disc.
                p.put( ox + px, py, 38, 42, 58 );
            }
        }
    }
}

void paint_rain( painter &p, int ox )
{
    // A thin slanted streak.
    for( int py = 2; py < cell - 2; ++py ) {
        const int x = cell / 2 + ( cell / 2 - py ) / 6;
        p.put( ox + x, py, 170, 190, 230 );
        p.put( ox + x + 1, py, 150, 170, 220 );
    }
}

void paint_snow( painter &p, int ox )
{
    // A six-armed flake.
    const float half = cell * 0.5f;
    for( int arm = 0; arm < 6; ++arm ) {
        const float a = static_cast<float>( arm ) * 1.0471976f;
        for( float t = 0.0f; t < half - 3.0f; t += 0.5f ) {
            const int x = static_cast<int>( half + std::cos( a ) * t );
            const int y = static_cast<int>( half + std::sin( a ) * t );
            p.put( ox + x, y, 245, 248, 255 );
            p.put( ox + x + 1, y, 245, 248, 255 );
        }
    }
    for( int dy = -2; dy <= 2; ++dy ) {
        for( int dx = -2; dx <= 2; ++dx ) {
            p.put( ox + cell / 2 + dx, cell / 2 + dy, 255, 255, 255 );
        }
    }
}

} // namespace

sky_sprites make_sky_sprites( gpu &device )
{
    sky_sprites out;
    SDL_Surface *const surface = SDL_CreateSurface( cell * sky_sprites::count, cell,
                                 SDL_PIXELFORMAT_RGBA32 );
    if( !surface ) {
        return out;
    }
    SDL_FillSurfaceRect( surface, nullptr, 0 );
    painter p{ surface };
    paint_sun( p, sky_sprites::sun * cell );
    for( int phase = 0; phase < 8; ++phase ) {
        paint_moon( p, ( sky_sprites::moon + phase ) * cell, phase );
    }
    paint_rain( p, sky_sprites::rain * cell );
    paint_snow( p, sky_sprites::snow * cell );
    out.sheet = device.create_sheet( surface );
    SDL_DestroySurface( surface );
    if( !out.sheet ) {
        return out;
    }
    const float w = static_cast<float>( cell * sky_sprites::count );
    for( int i = 0; i < sky_sprites::count; ++i ) {
        sprite_ref &r = out.cells[static_cast<size_t>( i )];
        r.sheet = out.sheet.get();
        r.owner = out.sheet;
        // Half a texel inside the cell so filtering never reads the neighbour.
        r.u0 = ( static_cast<float>( i * cell ) + 0.5f ) / w;
        r.u1 = ( static_cast<float>( ( i + 1 ) * cell ) - 0.5f ) / w;
        r.v0 = 0.5f / cell;
        r.v1 = 1.0f - 0.5f / cell;
        r.valid = true;
    }
    return out;
}

time_point sky_time()
{
    // Debug aid for screenshots: CATA_R3D_TIME_SHIFT=<hours> draws the sky that many hours
    // later than the game's time. Nothing else is affected.
    static const int shift = []() {
        const char *env = std::getenv( "CATA_R3D_TIME_SHIFT" );
        return env ? std::atoi( env ) : 0;
    }
    ();
    return calendar::turn + time_duration::from_hours( shift );
}

atmosphere current_atmosphere( int view_z, bool with_weather )
{
    atmosphere a;
    const float full = std::max( default_daylight_level(), 1.0f );
    a.daylight = std::clamp( sun_moon_light_at( sky_time() ) / full, 0.0f, 1.0f );
    const weather_manager &weather = get_weather_const();
    // Debug aid for screenshots: CATA_R3D_WEATHER=<weather type id> draws that weather instead
    // of the current one. Nothing else is affected.
    static const char *const forced_env = std::getenv( "CATA_R3D_WEATHER" );
    const weather_type_id weather_id = forced_env ? weather_type_id( forced_env ) :
                                       weather.weather_id;
    const bool weather_here = with_weather && view_z >= 0 && weather_id.is_valid() &&
                              weather_id != WEATHER_NULL;
    if( weather_here ) {
        const weather_type &w = *weather_id;
        // Cloud from how much the weather takes off the ambient light (fog: -40, storms -45).
        a.cloud = std::clamp( -static_cast<float>( w.light_modifier ) / 45.0f, 0.0f, 1.0f );
        a.fog_density = fog_density( w.sight_penalty );
        a.particle_id = w.tiles_animation;
        a.snow = !w.rains && w.precip != precip_class::none;
        switch( w.precip ) {
            case precip_class::very_light:
                a.particles = 140;
                break;
            case precip_class::light:
                a.particles = 320;
                break;
            case precip_class::heavy:
                a.particles = 700;
                break;
            default:
                a.particles = 0;
                break;
        }
        if( w.sound_category == weather_sound_category::rainstorm ||
            w.sound_category == weather_sound_category::thunder ||
            w.sound_category == weather_sound_category::snowstorm ) {
            a.particles = 1000;
        }
        if( a.particles > 0 && a.particle_id.empty() ) {
            a.particle_id = a.snow ? "weather_snowflake" : "weather_rain_drop";
        }
        // Wind in miles per hour; one tile is about a metre.
        constexpr float mph_to_tiles_per_second = 0.447f;
        a.wind = wind_velocity( weather.winddirection,
                                static_cast<float>( weather.windspeed ) * mph_to_tiles_per_second );
    }
    if( view_z < 0 ) {
        // Underground there is no sky; keep the near-black the view always had.
        a.sky = { 0.05f, 0.05f, 0.07f };
    } else {
        a.sky = sky_color( a.daylight, a.cloud, weather_here && weather.lightning_active );
    }
    // Fog is lighter than the sky by day (mist glows) and just as dark at night.
    const float grey = ( a.sky.r + a.sky.g + a.sky.b ) / 3.0f;
    const float lift = 0.15f * a.daylight;
    a.fog_color = { std::min( 1.0f, ( a.sky.r + grey ) * 0.5f + lift ),
                    std::min( 1.0f, ( a.sky.g + grey ) * 0.5f + lift ),
                    std::min( 1.0f, ( a.sky.b + grey ) * 0.5f + lift )
                  };
    return a;
}

std::vector<sky_body> sky_bodies( int view_z, const atmosphere &air )
{
    std::vector<sky_body> out;
    if( view_z < 0 || air.cloud > 0.4f ) {
        return out;
    }
    const std::pair<units::angle, units::angle> sun = sun_azimuth_altitude( sky_time() );
    const float azimuth = static_cast<float>( to_radians( sun.first ) );
    const float altitude = static_cast<float>( to_radians( sun.second ) );
    if( altitude > -0.05f ) {
        out.push_back( { sky_direction( azimuth, altitude ), sky_sprites::sun, true } );
    }
    // The sundial puts the moon opposite the sun; it is up when the sun is down.
    if( -altitude > -0.05f ) {
        const int phase = static_cast<int>( get_moon_phase( sky_time() ) );
        out.push_back( { sky_direction( azimuth + 3.14159265f, -altitude ),
                         sky_sprites::moon + std::clamp( phase, 0, 7 ), false } );
    }
    return out;
}

namespace
{
// Bands of wind speed (mph) that are worth a message when the wind moves between them.
int wind_band( int mph )
{
    return mph < 3 ? 0 : mph < 13 ? 1 : mph < 25 ? 2 : 3;
}
} // namespace

void report_weather_changes()
{
    static weather_type_id last_weather = WEATHER_NULL;
    static int last_band = -1;
    const weather_manager &weather = get_weather_const();
    const avatar &you = get_avatar();
    // Only what the character can tell: the weather overhead, from outdoors or a window.
    if( you.pos_bub().z() < 0 || !can_creature_see_sky( you ) ) {
        return;
    }
    if( weather.weather_id != last_weather && weather.weather_id.is_valid() &&
        weather.weather_id != WEATHER_NULL ) {
        if( last_weather != WEATHER_NULL ) {
            add_msg( m_info, _( "The weather turns: %s." ), weather.weather_id->name );
        }
        last_weather = weather.weather_id;
    }
    const int band = wind_band( weather.windspeed );
    if( band != last_band ) {
        if( last_band >= 0 ) {
            if( band == 0 ) {
                add_msg( m_info, _( "The wind dies down." ) );
            } else if( band > last_band ) {
                add_msg( m_info, _( "The wind picks up from the %s." ),
                         get_dirstring( weather.winddirection ) );
            } else {
                add_msg( m_info, _( "The wind eases, blowing from the %s." ),
                         get_dirstring( weather.winddirection ) );
            }
        }
        last_band = band;
    }
}

} // namespace r3d

#endif // defined(TILES)
