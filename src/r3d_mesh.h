#pragma once
#ifndef CATA_SRC_R3D_MESH_H
#define CATA_SRC_R3D_MESH_H

#if defined(TILES)

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "coordinates.h"
#include "r3d_gpu.h"
#include "r3d_shapes.h"
#include "type_id.h"

class cata_tiles;
class submap;

namespace r3d
{

/** A tileset sprite as sampled by the 3D renderer: its sheet and the uv rectangle in it. */
struct sprite_ref {
    SDL_Texture *sheet = nullptr;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool valid = false;
};

struct tile_info {
    shape sh;
    sprite_ref sprite;
};

/**
 * Per terrain/furniture id lookups (shape + sprite), so the mesher does not walk the tileset
 * once per tile. Must be cleared whenever the tileset or the renderer's textures change.
 */
class tile_info_cache
{
    public:
        void clear();
        const tile_info &terrain( ter_id id, cata_tiles &tiles );
        const tile_info &furniture( furn_id id, cata_tiles &tiles );
        /** Ground drawn under trees and bushes, which are terrain with a transparent background. */
        const sprite_ref &ground( cata_tiles &tiles );

    private:
        std::unordered_map<int, tile_info> terrain_;
        std::unordered_map<int, tile_info> furniture_;
        sprite_ref ground_;
        bool ground_loaded_ = false;
};

/** Where a vertex gets its light from: the tile at (lx + dx, ly + dy) on level (chunk z + dz). */
struct light_ref {
    uint8_t lx = 0;
    uint8_t ly = 0;
    int8_t dx = 0;
    int8_t dy = 0;
    int8_t dz = 0;
    uint8_t shade = 255; // directional shading baked into the face, 255 = full brightness
};

struct mesh_batch {
    SDL_Texture *sheet = nullptr;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
};

/** CPU-side geometry of one submap at one z-level, in chunk-local world coordinates. */
struct chunk_mesh {
    std::vector<vertex> vertices;
    std::vector<uint32_t> light;      // RGBA8 per vertex, parallel to vertices
    std::vector<light_ref> light_refs; // parallel to vertices
    std::vector<uint32_t> indices;
    std::vector<mesh_batch> batches;

    bool empty() const {
        return indices.empty();
    }
};

using submap_lookup = std::function<const submap *( const tripoint_abs_sm & )>;

/** Combined change stamp of a submap and the neighbours whose shapes affect its faces. */
uint64_t chunk_stamp( const tripoint_abs_sm &pos, const submap_lookup &lookup );

/** Packs a grey level into the RGBA8 light format of stream 1. */
uint32_t pack_light( uint8_t r, uint8_t g, uint8_t b );

/** Meshes the terrain and furniture of one submap. Returns an empty mesh if it is not loaded. */
chunk_mesh build_chunk_mesh( const tripoint_abs_sm &pos, const submap_lookup &lookup,
                             tile_info_cache &cache, cata_tiles &tiles );

} // namespace r3d

#endif // defined(TILES)

#endif // CATA_SRC_R3D_MESH_H
