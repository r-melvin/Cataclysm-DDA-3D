#include "r3d_shapes.h"

namespace r3d
{

namespace
{
constexpr float tree_height = 1.4f;
constexpr float shrub_height = 0.7f;
constexpr float tiny_height = 0.25f;
constexpr float low_height = 0.45f;
constexpr float surface_height = 0.55f;
constexpr float default_furniture_height = 0.7f;
constexpr float tall_furniture_height = 0.9f;
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
    if( t.window && t.move_cost > 0 ) {
        return { shape_kind::pane, 1.0f }; // empty or broken window frame
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
    // Furniture sprites are drawn top-down with transparent margins, so wrapping them around a box
    // gives hollow shells. Tall impassable furniture (lockers, fridges) is drawn by tilesets as
    // seen from the front, so it stands upright; the rest is a sprite lying on top of the
    // furniture's height.
    const float height = t.tiny ? tiny_height : t.low ? low_height : t.flat_surface ? surface_height :
                         t.move_cost <= 0 ? tall_furniture_height : default_furniture_height;
    if( t.move_cost <= 0 && !t.tiny && !t.low && !t.flat_surface ) {
        return { shape_kind::upright, height };
    }
    return { shape_kind::floor, height };
}

namespace
{

bool hides_side( const shape &self, const shape &other )
{
    const bool solid = other.kind == shape_kind::block || other.kind == shape_kind::prism;
    return solid && other.height >= self.height;
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
            if( n.up.kind != shape_kind::block ) {
                faces |= face_top;
            }
            return faces;
        case shape_kind::block:
        case shape_kind::prism:
            break;
    }
    if( n.up.kind != shape_kind::block ) {
        faces |= face_top;
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
