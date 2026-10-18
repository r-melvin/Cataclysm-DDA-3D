#pragma once
#ifndef CATA_SRC_R3D_SHAPES_H
#define CATA_SRC_R3D_SHAPES_H

#include <array>
#include <cstdint>

namespace r3d
{

/** How a terrain or furniture tile is turned into geometry. */
enum class shape_kind : uint8_t {
    empty,  // nothing is drawn (open air)
    floor,  // thin slab: only a top face
    block,  // full-height cube
    prism,  // box of a given height
    upright, // sprite standing on the floor, turned towards the camera (trees, lockers)
    pane,   // upright panel through the middle of the cell along a wall line (broken windows)
};

struct shape {
    shape_kind kind = shape_kind::empty;
    float height = 0.0f; // top of the solid part within the cell, in tile units
    float slab = 0.0f;   // prism only: > 0 is a top this thick on legs (benches, tables)

    bool operator==( const shape &o ) const {
        return kind == o.kind && height == o.height;
    }
};

constexpr float floor_height = 0.1f;
// Shared half-thickness of walls, doors and window surrounds, in tiles.
constexpr float wall_half_thickness = 0.08f;
// Height (in levels, from the floor's base) that walls and tall furniture are cut down to
// around the player in the orbit view; they keep a cap there so they read as low walls.
constexpr float cutaway_stub_height = 0.4f;

/** The terrain/furniture properties that decide a tile's shape, flattened from ter_t / furn_t. */
struct tile_traits {
    bool no_floor = false;
    bool wall = false;
    bool transparent = false;
    bool tree = false;
    bool shrub = false;
    bool flat = false;
    bool tiny = false;
    bool low = false;       // TFLAG_SHORT
    bool flat_surface = false;
    bool panel = false;     // complete front elevation, such as a notice board
    bool container = false; // furniture that holds items inside (cabinets): solid, not on legs
    bool window = false;
    bool door = false;      // a door in a wall line, open or closed
    int move_cost = 100;    // <= 0: cannot be walked through
};

shape classify_terrain( const tile_traits &t );

/**
 * How a 3/4-view furniture sprite becomes a box. Tilesets draw furniture with its lid above its
 * front, inside transparent margins; the box takes the opaque part's width, its lid rows become
 * the box's top and some rows below them its sides. All values are fractions: footprint in the
 * tile (x east, y south, before any rotation), rows and columns of the sprite.
 */
struct box_layout {
    float x0 = 0.0f;
    float x1 = 1.0f;
    float y0 = 0.0f;
    float y1 = 1.0f;
    float col0 = 0.0f;
    float col1 = 1.0f;
    float lid_top = 0.0f;
    float lid_bottom = 1.0f;
    float side_top = 0.0f;
    float side_bottom = 1.0f;
};

/**
 * Layout for a sprite whose opaque pixels span columns [ox0, ox1] and rows [oy0, oy1] (fractions
 * of the sprite). front is the share of the opaque rows that shows the front. Low furniture
 * (benches) repeats the lid's front edge on its sides, since its front rows are mostly legs.
 */
box_layout furniture_box_layout( float ox0, float oy0, float ox1, float oy1, float front, bool low );
shape classify_furniture( const tile_traits &t );

enum face_bit : uint8_t {
    face_top = 1,
    face_north = 2, // -y in tile space
    face_south = 4,
    face_west = 8,
    face_east = 16,
};

/** Occupied cells of a 3x3 grid split at the two faces of the centred wall.
 *  The centre joins arms reaching connected tile boundaries (face_bit mask). */
std::array<bool, 9> wall_footprint( uint8_t connections );

/** An indoor one-tile spur attached to the inside of an exterior corner. */
bool trim_wall_spur( uint8_t connections, uint8_t indoor_sides, uint8_t neighbour_connections );

/** Choose a cabinet back wall, preferring the wall along its row. */
int furniture_wall_facing( bool row_x, bool row_y, uint8_t walls );

/** Furniture bounds after mirroring, rotation and joining, in tile coordinates. Solid
 *  furniture can extend into the unused part of an adjacent thin wall tile to meet its face. */
box_layout place_furniture_box( const box_layout &box, bool flip, int facing,
                                uint8_t joined, uint8_t wall_contacts, float depth_scale = 1.0f );

/** Neighbouring shapes around a cell, in tile space. */
struct neighbourhood {
    shape up;
    shape north;
    shape south;
    shape west;
    shape east;
};

/**
 * Which faces of a shape are worth emitting given what surrounds it. Bottom faces never are; top
 * faces always are, since the 3D view may cut the levels above away.
 */
uint8_t visible_faces( const shape &self, const neighbourhood &n );

} // namespace r3d

#endif // CATA_SRC_R3D_SHAPES_H
