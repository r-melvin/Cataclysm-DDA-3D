#pragma once
#ifndef CATA_SRC_R3D_H
#define CATA_SRC_R3D_H

// Public entry points of the 3D renderer. No SDL types here so game code can include it in
// every build; without TILES everything is an inline no-op.

#if defined(TILES)
#include "coords_fwd.h"

struct SDL_Renderer;
#endif

namespace r3d
{

#if defined(TILES)

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

/** Flips the RENDERER_3D option. */
void toggle();
/** Orbit the camera by one step; dir < 0 is left, dir > 0 right. */
void rotate( int dir );
/** Tilt the camera by one step; dir > 0 raises the view towards top-down. */
void pitch( int dir );
/** Move the camera closer (dir > 0) or away (dir < 0). */
void zoom( int dir );
/** Restore the default camera. */
void reset();

#else

inline bool active()
{
    return false;
}
inline void toggle() {}
inline void rotate( int ) {}
inline void pitch( int ) {}
inline void zoom( int ) {}
inline void reset() {}

#endif

} // namespace r3d

#endif // CATA_SRC_R3D_H
