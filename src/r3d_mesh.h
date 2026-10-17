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
#include "r3d_wall_rules.h"
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
    float size_x = 1.0f; // sprite size in tiles; tall sprites (size_y > size_x) overflow upwards
    float size_y = 1.0f;
    int quarter_turns = 0; // clockwise, applied to top faces like the flat view turns the sprite
    int facing = 0;        // clockwise quarter turns the front of the thing faces from south
    // how far the flat view shifts the sprite from its tile, in tiles (x right, y down), so
    // layers of different sheet heights line up as they do there
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    bool flip = false;     // mirrored horizontally
    uint32_t top_color = 0;  // average colours from the tileset loader (see sprite_scan)
    uint32_t body_color = 0;
    // opaque part of the sprite as fractions of it (whole sprite when unknown)
    float opaque_x0 = 0.0f;
    float opaque_y0 = 0.0f;
    float opaque_x1 = 1.0f;
    float opaque_y1 = 1.0f;
    bool valid = false;
};

struct tile_info {
    shape sh;
    sprite_ref backdrop; // opaque background layer, drawn first when the tileset has one
    sprite_ref sprite;   // foreground layer (alpha tested, may be sparse)
    bool tree = false;   // the level above holds its treetop, which this sprite already shows
    sprite_ref side;     // texture for the sides of a wall (see wall_sides), else invalid
    sprite_ref cap;      // the top of such a wall: a plain patch of the same texture
    uint32_t side_tint = 0xffffffu; // 0xRRGGBB colours for the side texture and the cap
    uint32_t cap_tint = 0xffffffu;
    bool see_through = false; // transparent terrain (fences, bars): the floor shows through it

    /** The layer that represents the tile when only one can be drawn (upright sprites). */
    const sprite_ref &main() const {
        return sprite.valid ? sprite : backdrop;
    }
};

/** A sprite that stands on a tile and turns towards the camera; emitted per frame. */
struct upright_sprite {
    uint8_t lx = 0; // chunk-local tile
    uint8_t ly = 0;
    float z_base = 0.0f;
    const tile_info *info = nullptr; // owned by the tile_info_cache, like the chunk's sheets
};

/** Side textures for walls: an atlas on the renderer and which wall gets which cell. */
struct wall_sides {
    std::shared_ptr<SDL_Texture> atlas;
    int width = 0;
    int height = 0;
    int cell = 0;
    std::vector<std::string> names; // atlas cells in order
    wall_texture_rules rules;
    // The cells are grey; a wall takes its colours from the tileset's sprite, or else from here
    // (0xRRGGBB by cell name).
    std::unordered_map<std::string, uint32_t> colors;
};

/**
 * Per terrain/furniture id lookups (shape + sprite), so the mesher does not walk the tileset
 * once per tile. Must be cleared whenever the tileset or the renderer's textures change.
 */
class tile_info_cache
{
    public:
        /** Forgets every lookup; the wall side textures stay. */
        void clear();
        /** Side textures for walls from now on (nullptr for the tileset sprite); clears. */
        void set_wall_sides( std::shared_ptr<const wall_sides> sides );
        /**
         * With subtile/rota (see cata_tiles::get_orientation) the sprites are those the flat view
         * draws for that connection; the shape never depends on them.
         */
        const tile_info &terrain( ter_id id, cata_tiles &tiles, int subtile = -1, int rota = 0 );
        const tile_info &furniture( furn_id id, cata_tiles &tiles, int subtile = -1, int rota = 0 );
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
        std::shared_ptr<const wall_sides> wall_sides_;
};

/** Where a vertex gets its light from: the tile at (lx + dx, ly + dy) on level (chunk z + dz). */
struct light_ref {
    uint8_t lx = 0;
    uint8_t ly = 0;
    int8_t dx = 0;
    int8_t dy = 0;
    int8_t dz = 0;
    uint8_t shade = 255; // directional shading baked into the face, 255 = full brightness
    // colour multiplied into the light (wall textures take the tileset's colours), 255 = none
    uint8_t tint_r = 255;
    uint8_t tint_g = 255;
    uint8_t tint_b = 255;
    // a floor top: shown as dark floor rather than left out where its tile was never seen,
    // so unexplored rooms are a dark floor between their seen walls, not a hole
    bool floor = false;
    // a cap drawn at the cutaway stub height: shown only inside the cutaway, where the tile
    // above it is removed, so a cut wall or locker reads as a low one rather than a stump
    bool stub = false;
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
    std::vector<upright_sprite> uprights;

    bool empty() const {
        return indices.empty() && uprights.empty();
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
        /** Same with a colour per corner (the alpha carries the billboards' depth pull). */
        void add_colored( const sprite_ref &sprite, const corners_t &corners, const uvs_t &uvs,
                          const light_ref &ref, const std::array<uint32_t, 4> &colors );
        chunk_mesh finish();

    private:
        struct quad {
            SDL_Texture *sheet = nullptr;
            std::array<vertex, 4> v;
            std::array<light_ref, 4> r;
            std::array<uint32_t, 4> colors{};
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
