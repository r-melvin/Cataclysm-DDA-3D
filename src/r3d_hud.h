#pragma once
#ifndef CATA_SRC_R3D_HUD_H
#define CATA_SRC_R3D_HUD_H

#if defined(TILES)

#include <optional>
#include <vector>

#include "coordinates.h"
#include "r3d.h"
#include "r3d_camera.h"
#include "r3d_sky.h"

struct SDL_Renderer;

namespace r3d
{

/** How the 3D picture just drawn maps the scene to the screen. */
struct hud_frame {
    float x = 0.0f; // the picture's rectangle, logical pixels
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    camera cam;
    mat4 view_proj;
    tripoint_bub_ms center; // the viewed tile: the origin of the scene frame
    bool sct = false;       // the game queued its scrolling combat text for this frame

    /** Clip position of a point in scene-frame tile space (x east, y south, z levels). */
    vec3 clip( const vec3 &tile, float &w_out ) const;
    /** Screen position (logical pixels) of that point; nothing when it is behind the camera. */
    std::optional<vec3> screen( const vec3 &tile ) const;
    /** Screen position of the middle of a map tile, height levels above its floor. */
    std::optional<vec3> screen( const tripoint_bub_ms &p, float height ) const;
    /** Scene-frame tile space position of the middle of a map tile, height levels up. */
    vec3 tile_point( const tripoint_bub_ms &p, float height ) const;
};

/**
 * Draws the overlays of the 3D view over its picture with the SDL renderer (shapes) and adds
 * the texts they need to texts (drawn by sdltiles in the map font). Each part follows its own
 * option and only shows what the player perceives.
 */
void draw_hud( SDL_Renderer *renderer, const hud_frame &frame, const atmosphere &air,
               std::vector<hud_text> &texts );

} // namespace r3d

#endif // defined(TILES)

#endif // CATA_SRC_R3D_HUD_H
