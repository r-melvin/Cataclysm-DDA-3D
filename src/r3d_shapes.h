#pragma once
#ifndef CATA_SRC_R3D_SHAPES_H
#define CATA_SRC_R3D_SHAPES_H

#include <cstdint>

namespace r3d
{

/** How a terrain or furniture tile is turned into geometry. */
enum class shape_kind : uint8_t {
    empty,  // nothing is drawn (open air)
    floor,  // thin slab: only a top face
    block,  // full-height cube
    prism,  // box of a given height
    cross,  // two crossed upright quads (trees, bushes)
};

struct shape {
    shape_kind kind = shape_kind::empty;
    float height = 0.0f; // top of the solid part within the cell, in tile units

    bool operator==( const shape &o ) const {
        return kind == o.kind && height == o.height;
    }
};

constexpr float floor_height = 0.1f;

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
    int move_cost = 100;    // <= 0: cannot be walked through
};

shape classify_terrain( const tile_traits &t );
shape classify_furniture( const tile_traits &t );

enum face_bit : uint8_t {
    face_top = 1,
    face_north = 2, // -y in tile space
    face_south = 4,
    face_west = 8,
    face_east = 16,
};

/** Neighbouring shapes around a cell, in tile space. */
struct neighbourhood {
    shape up;
    shape north;
    shape south;
    shape west;
    shape east;
};

/** Which faces of a shape are worth emitting given what surrounds it. Bottom faces never are. */
uint8_t visible_faces( const shape &self, const neighbourhood &n );

} // namespace r3d

#endif // CATA_SRC_R3D_SHAPES_H
