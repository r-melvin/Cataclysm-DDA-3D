#pragma once
#ifndef CATA_SRC_R3D_SKY_H
#define CATA_SRC_R3D_SKY_H

#if defined(TILES)

#include <array>
#include <memory>
#include <string>

#include "calendar.h"
#include "r3d_hud_math.h"
#include "r3d_mesh.h"

namespace r3d
{

class gpu;

/**
 * Sprites the 3D view draws itself, for things a tileset has no sprite for: the sun, the moon in
 * its eight phases (indexed like moon_phase), and a rain streak and a snowflake used when the
 * tileset lacks weather_rain_drop / weather_snowflake.
 */
struct sky_sprites {
    static constexpr int sun = 0;
    static constexpr int moon = 1; // + moon_phase
    static constexpr int rain = 9;
    static constexpr int snow = 10;
    static constexpr int count = 11;

    std::shared_ptr<SDL_Texture> sheet;
    std::array<sprite_ref, count> cells;

    bool loaded() const {
        return sheet != nullptr;
    }
};

/** Draws the sprites above into a sheet on the 3D renderer; empty when that fails. */
sky_sprites make_sky_sprites( gpu &device );

/** The time the sky is drawn for: the game's (see CATA_R3D_TIME_SHIFT in r3d_sky.cpp). */
time_point sky_time();

/** What the weather and the time of day look like this frame (see current_atmosphere). */
struct atmosphere {
    rgb sky;                 // colour of the sky (time of day, cloud, lightning)
    float daylight = 0.0f;   // share of full daylight, 0..1
    float cloud = 0.0f;      // 0 clear .. 1 the darkest storm
    rgb fog_color;
    float fog_density = 0.0f; // see fog_density() in r3d_hud_math.h; 0 for none
    std::string particle_id;  // tileset sprite of the falling particles, empty for none
    bool snow = false;        // particles fall slowly as flakes, not as rain
    int particles = 0;        // how many fill a field of the default size
    vec3 wind;                // world velocity of the wind, tiles per second
};

/**
 * The atmosphere over the viewed level view_z. Weather (particles, fog, cloud) only exists at
 * or above ground; underground the sky is dark and nothing falls. with_weather false gives a
 * clear sky (the option is off).
 */
atmosphere current_atmosphere( int view_z, bool with_weather );

/**
 * Writes weather and wind changes the character can perceive to the message log ("The wind
 * picks up from the North-West."), the text equivalent of what the 3D view shows. Called once per
 * 3D frame; the first call only notes the current state.
 */
void report_weather_changes();

} // namespace r3d

#endif // defined(TILES)

#endif // CATA_SRC_R3D_SKY_H
