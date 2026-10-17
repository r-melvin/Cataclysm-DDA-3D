#pragma once
#ifndef CATA_SRC_R3D_ENTITIES_H
#define CATA_SRC_R3D_ENTITIES_H

#if defined(TILES)

#include <cstdint>
#include <functional>
#include <vector>

#include "coordinates.h"
#include "r3d_camera.h"
#include "r3d_mesh.h"

class cata_tiles;

namespace r3d
{

/** Largest depth pull of a billboard top, in stretched units; the draw passes it as pull.w. */
constexpr float max_billboard_pull = 8.0f;

struct entity_view {
    cata_tiles &tiles;
    tile_info_cache &cache;
    const camera &cam;
    tripoint_bub_ms center; // the viewed tile, which is the origin of the scene frame
    int z_lo = 0;
    int z_hi = 0;
    /** Light colour (RGBA8, see pack_light) of a tile; black means it cannot be seen. */
    std::function<uint32_t( const tripoint_bub_ms & )> light;

    /** UI marker (cursor, highlight, line) laid on top of a tile's surface, always unlit. */
    struct marker {
        tripoint_bub_ms pos;
        float top = 0.0f;           // surface height within the tile, see column_height
        const tile_info *sprite = nullptr;
        bool box = false;           // also draw the sprite on the sides of a box above the tile
    };
    std::vector<marker> markers;

    /** Upright sprites of the visible chunks (trees, lockers), at a tile of the map. */
    struct standing {
        tripoint_bub_ms pos;
        float z_base = 0.0f; // above the tile's level
        const tile_info *info = nullptr;
    };
    std::vector<standing> uprights;

    /** Small camera-facing sprites (rain, snow), unlit by the map: their colour is given. */
    struct particle {
        float x = 0.0f; // scene-frame tile space (x east, y south, z up in levels)
        float y = 0.0f;
        float z = 0.0f;
        float size = 0.3f; // width and height in tiles
        const sprite_ref *sprite = nullptr;
        uint32_t color = 0;
    };
    std::vector<particle> particles;

    /** Height within its level of what stands on a tile: the floor, or a bench or counter top. */
    std::function<float( const tripoint_bub_ms & )> surface;
};

/**
 * Billboards for creatures, the player and loose items near the viewed tile, in the scene frame
 * (so drawn with a zero offset). Rebuilt every frame since they move.
 */
chunk_mesh build_entity_mesh( const entity_view &view );

/**
 * Camera-facing quads for view.particles, in the scene frame. Kept apart from the entities: they
 * are drawn with dithered transparency (block_uniforms::fog2).
 */
chunk_mesh build_particle_mesh( const entity_view &view );

} // namespace r3d

#endif // defined(TILES)

#endif // CATA_SRC_R3D_ENTITIES_H
