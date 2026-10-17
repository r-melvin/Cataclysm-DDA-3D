#pragma once
#ifndef CATA_SRC_R3D_H
#define CATA_SRC_R3D_H

// Public entry points of the 3D renderer. No SDL types here so game code can include it in
// every build; without TILES everything is an inline no-op.

#if defined(TILES)
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"

struct SDL_Renderer;
#endif

namespace r3d
{

#if defined(TILES)

/** A line of text the 3D view wants drawn over its last frame, in the map font. */
struct hud_text {
    float x = 0.0f; // logical pixels, in the space of the rectangle given to draw()
    float y = 0.0f; // top of the text
    std::string text;
    int color = 7;  // curses colour index (0-15), as for overlay strings
    int align = 0;  // -1: starts at x, 0: centred on x, 1: ends at x
    bool boxed = false; // drawn on a dark box, for text over a busy picture
};

/** Texts to draw over the 3D frame the last draw() returned true for. */
const std::vector<hud_text> &hud_texts();

/**
 * Draws the map around center into the pixel rectangle (x, y, w, h) of the renderer's current
 * target, composited like any other 2D draw. Returns false when the 3D view cannot be drawn this
 * frame (option off, no GPU renderer, ...), in which case the caller draws the flat view instead.
 * render_epoch changes whenever the renderer or its textures were rebuilt.
 */
bool draw( SDL_Renderer *renderer, float x, float y, float w, float h,
           const tripoint_bub_ms &center, unsigned render_epoch );

/** Releases all GPU objects; call before the renderer goes away. */
void shutdown();

/** True when the option is on, the GPU renderer is available and the 3D scene is drawn. */
bool active();

/**
 * The map tile under the point (x, y) of the renderer, in the same logical pixels as the
 * rectangle given to draw(), as seen in the last drawn 3D frame. Like the flat view's picks, the
 * result is on the map's own level (get_abs_sub().z()): something seen through a hole on a lower
 * level yields the tile where the mouse ray crossed the viewed level. Nothing when the point is
 * outside that frame, points at the sky, or the 3D view is not active.
 */
std::optional<tripoint_bub_ms> pick( float x, float y );

/**
 * 1/8 turns clockwise to apply to map directions so that "north" walks away from the 3D camera
 * (see camera::forward_eighths); 0 when the 3D view is not shown.
 */
int view_eighths();
/** True when the main game takes WASD for walking and the arrow keys for the 3D camera. */
bool wasd_controls();

/**
 * Debug capture for CATA_R3D_COMPARE (see r3d.cpp): saves the 3D view and the flat isometric
 * view of the same turn once. Called from the input loop while waiting for input.
 */
void compare_capture();

/** Flips the RENDERER_3D option. */
void toggle();
/** Orbit the camera by one step; dir < 0 is left, dir > 0 right. */
void rotate( int dir );
/** Tilt the camera by one step; dir > 0 raises the view towards top-down. */
void pitch( int dir );
/** Move the camera closer (dir > 0) or away (dir < 0). */
void zoom( int dir );
/** Restore the default camera, centred on the view again. */
void reset();
/** True when (x, y), in the logical pixels given to draw(), lies on the last drawn 3D frame. */
bool contains( float x, float y );
/** Free camera from a mouse drag of (dx, dy) pixels: orbit and tilt, or slide over the ground. */
void drag_orbit( float dx, float dy );
void drag_pan( float dx, float dy );

#else

inline bool active()
{
    return false;
}
inline int view_eighths()
{
    return 0;
}
inline bool wasd_controls()
{
    return false;
}
inline void toggle() {}
inline void rotate( int ) {}
inline void pitch( int ) {}
inline void zoom( int ) {}
inline void reset() {}
inline bool contains( float, float )
{
    return false;
}
inline void drag_orbit( float, float ) {}
inline void drag_pan( float, float ) {}

#endif

} // namespace r3d

#endif // CATA_SRC_R3D_H
