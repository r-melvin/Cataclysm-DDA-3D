#pragma once
#ifndef CATA_SRC_R3D_MESH_H
#define CATA_SRC_R3D_MESH_H

#if defined(TILES)

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "coordinates.h"
#include "r3d_gpu.h"
#include "r3d_shapes.h"
#include "type_id.h"

class cata_tiles;
class submap;
enum class TILE_CATEGORY;

namespace r3d
{

/** A tileset sprite as sampled by the 3D renderer: its sheet and the uv rectangle in it. */
struct sprite_ref {
    SDL_Texture *sheet = nullptr;
    // Holds the sheet alive while it is cached, so a tileset reload can never leave a dangling
    // pointer here or in a chunk's mesh_batch (chunks are dropped whenever the cache is cleared).
    std::shared_ptr<SDL_Texture> owner;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool valid = false;
};

struct tile_info {
    shape sh;
    sprite_ref backdrop; // opaque background layer, drawn first when the tileset has one
    sprite_ref sprite;   // foreground layer (alpha tested, may be sparse)
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
        /** Sprite layers by tileset id, e.g. a monster type or "player_male". */
        const tile_info &named( TILE_CATEGORY category, const std::string &id, cata_tiles &tiles );
        /** Ground drawn under trees and bushes, which are terrain with a transparent background. */
        const tile_info &ground( cata_tiles &tiles );

    private:
        std::unordered_map<int, tile_info> terrain_;
        std::unordered_map<int, tile_info> furniture_;
        std::unordered_map<std::string, tile_info> named_;
        tile_info ground_;
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

using corners_t = std::array<std::array<float, 3>, 4>;
using uvs_t = std::array<std::array<float, 2>, 4>;

/**
 * Collects textured quads and turns them into one indexed mesh grouped by sprite sheet. Corners
 * are given in tile space (x east, y south, z up) and flipped into the world frame here.
 */
class quad_sink
{
    public:
        /** Quad lit by its shade only; the light stream can later be recomputed from ref. */
        void add( const sprite_ref &sprite, const corners_t &corners, const uvs_t &uvs,
                  const light_ref &ref );
        /** Quad with an explicit light colour (used by the dynamic entity geometry). */
        void add_colored( const sprite_ref &sprite, const corners_t &corners, const uvs_t &uvs,
                          const light_ref &ref, uint32_t color );
        chunk_mesh finish();

    private:
        struct quad {
            SDL_Texture *sheet = nullptr;
            std::array<vertex, 4> v;
            std::array<light_ref, 4> r;
            uint32_t color = 0;
        };
        std::vector<quad> quads_;
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
