#include "r3d_shapes.h"

#include <algorithm>

namespace r3d
{

namespace
{
constexpr float tree_height = 1.4f;
constexpr float shrub_height = 0.7f;
// Furniture heights are in z-levels, which the view draws about 1.6-1.8 tiles (2.5 m) tall: a
// bench seat about knee high as the isometric tilesets draw it, a table or counter ~0.75 m, a
// locker ~1.8 m.
constexpr float tiny_height = 0.15f;
constexpr float low_height = 0.36f;
constexpr float surface_height = 0.4f;
constexpr float default_furniture_height = 0.5f;
constexpr float tall_furniture_height = 0.9f;
constexpr float slab_thickness = 0.06f;
// Floor-like furniture (rugs, carpets) sits just above the terrain floor.
constexpr float decal_height = floor_height + 0.01f;
} // namespace

shape classify_terrain( const tile_traits &t )
{
    if( t.no_floor ) {
        return { shape_kind::empty, 0.0f };
    }
    if( t.tree ) {
        return { shape_kind::upright, tree_height };
    }
    if( t.shrub ) {
        return { shape_kind::upright, shrub_height };
    }
    // Windows and closed doors stand in the wall line as panels rather than filling the tile.
    if( !t.wall && ( t.window || ( t.door && !t.flat ) ) ) {
        return { shape_kind::pane, 1.0f };
    }
    if( t.wall || ( t.move_cost <= 0 && !t.flat ) ) {
        return { shape_kind::block, 1.0f };
    }
    return { shape_kind::floor, floor_height };
}

shape classify_furniture( const tile_traits &t )
{
    if( t.flat ) {
        return { shape_kind::floor, decal_height };
    }
    if( t.tiny ) {
        return { shape_kind::floor, tiny_height };
    }
    // Solid furniture is a box: tilesets draw it in 3/4 view, so the top of the sprite becomes
    // the box's top and the rest its sides. Low or flat-topped pieces (benches, counters, tables)
    // are low boxes, other impassable ones (lockers, fridges) tall ones.
    if( t.low || t.flat_surface ) {
        const float height = t.flat_surface ? surface_height : low_height;
        // Benches and tables are a top on legs; cabinets and counters holding items are solid.
        return { shape_kind::prism, height, t.container ? 0.0f : slab_thickness };
    }
    if( t.move_cost <= 0 ) {
        return { shape_kind::prism, tall_furniture_height };
    }
    // Small passable things (chairs, plants) stand up as a sprite.
    return { shape_kind::upright, default_furniture_height };
}

box_layout furniture_box_layout( float ox0, float oy0, float ox1, float oy1, float front, bool low )
{
    box_layout b;
    if( ox1 - ox0 < 0.05f || oy1 - oy0 < 0.05f ) {
        // no usable bounds: the whole sprite on a whole tile
        ox0 = 0.0f;
        oy0 = 0.0f;
        ox1 = 1.0f;
        oy1 = 1.0f;
    }
    const float span = oy1 - oy0;
    const float split = oy1 - span * front;
    b.col0 = ox0;
    b.col1 = ox1;
    b.x0 = ox0;
    b.x1 = ox1;
    // Furniture art stands with its back to the north (turned pieces turn with the box), so the
    // box starts at the back of the tile, against whatever wall is there. Its depth follows the
    // lid rows, which cover the tile's depth over the rows above the front of a full sprite.
    const float depth_rows = std::max( 1.0f - front, 0.05f );
    b.y0 = 0.0f;
    b.y1 = std::clamp( ( split - oy0 ) / depth_rows, 0.5f, 1.0f );
    b.lid_top = oy0;
    b.lid_bottom = split;
    if( low ) {
        constexpr float lid_edge = 0.12f;
        b.side_top = std::max( oy0, split - span * lid_edge );
        b.side_bottom = split;
    } else {
        b.side_top = split;
        b.side_bottom = oy1;
    }
    return b;
}

namespace
{

bool hides_side( const shape &self, const shape &other )
{
    const bool solid = other.kind == shape_kind::block || other.kind == shape_kind::prism;
    return solid && other.height >= self.height;
}

// A cell whose level above has a floor, or a wall, has a ceiling; open air above means sky.
uint8_t ceiling_under( const shape &up )
{
    return up.kind == shape_kind::floor || up.kind == shape_kind::block ||
           up.kind == shape_kind::prism ? face_ceiling : 0;
}

} // namespace

uint8_t visible_faces( const shape &self, const neighbourhood &n )
{
    uint8_t faces = 0;
    switch( self.kind ) {
        case shape_kind::empty:
        case shape_kind::upright:
        case shape_kind::pane:
            return 0;
        case shape_kind::floor:
            // Tops are kept even under a block: the levels above may be cut away.
            return static_cast<uint8_t>( face_top | ceiling_under( n.up ) );
        case shape_kind::block:
        case shape_kind::prism:
            break;
    }
    faces |= face_top;
    if( self.kind == shape_kind::prism ) {
        faces |= ceiling_under( n.up );
    }
    if( !hides_side( self, n.north ) ) {
        faces |= face_north;
    }
    if( !hides_side( self, n.south ) ) {
        faces |= face_south;
    }
    if( !hides_side( self, n.west ) ) {
        faces |= face_west;
    }
    if( !hides_side( self, n.east ) ) {
        faces |= face_east;
    }
    return faces;
}

} // namespace r3d
