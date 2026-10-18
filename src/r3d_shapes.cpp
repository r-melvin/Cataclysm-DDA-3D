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
        return { shape_kind::upright, shrub_height, 0.0f, true };
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
    if( t.panel ) {
        return { shape_kind::pane, tall_furniture_height };
    }
    if( t.flat ) {
        return { shape_kind::floor, decal_height };
    }
    // Natural obstacles need a filled volume, not the lid/front/legs of manufactured
    // furniture. Use the same simple box for plants and rocks, with size-based heights.
    if( t.shrub || t.boulder ) {
        const float height = t.tiny ? 0.3f : t.low ? 0.55f : tall_furniture_height;
        return { shape_kind::block, height };
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

box_layout place_furniture_box( const box_layout &box, bool flip, int facing,
                                uint8_t joined, uint8_t wall_contacts, float depth_scale )
{
    box_layout b = box;
    b.x0 = flip ? 1.0f - box.x1 : box.x0;
    b.x1 = flip ? 1.0f - box.x0 : box.x1;
    for( int turn = 0; turn < ( facing & 3 ); ++turn ) {
        const float x0 = 1.0f - b.y1;
        const float x1 = 1.0f - b.y0;
        b.y0 = b.x0;
        b.y1 = b.x1;
        b.x0 = x0;
        b.x1 = x1;
    }
    if( ( joined & face_north ) != 0 ) {
        b.y0 = 0.0f;
    }
    if( ( joined & face_south ) != 0 ) {
        b.y1 = 1.0f;
    }
    if( ( joined & face_west ) != 0 ) {
        b.x0 = 0.0f;
    }
    if( ( joined & face_east ) != 0 ) {
        b.x1 = 1.0f;
    }
    constexpr float gap = 0.5f - wall_half_thickness;
    if( ( wall_contacts & face_north ) != 0 ) {
        b.y0 = -gap;
    }
    if( ( wall_contacts & face_south ) != 0 ) {
        b.y1 = 1.0f + gap;
    }
    if( ( wall_contacts & face_west ) != 0 ) {
        b.x0 = -gap;
    }
    if( ( wall_contacts & face_east ) != 0 ) {
        b.x1 = 1.0f + gap;
    }
    // Reduce depth after fitting to the wall so the back stays flush in every orientation.
    const float depth_x = ( b.x1 - b.x0 ) * depth_scale;
    const float depth_y = ( b.y1 - b.y0 ) * depth_scale;
    switch( facing & 3 ) {
        case 0:
            b.y1 = b.y0 + depth_y;
            break;
        case 1:
            b.x0 = b.x1 - depth_x;
            break;
        case 2:
            b.y0 = b.y1 - depth_y;
            break;
        case 3:
            b.x1 = b.x0 + depth_x;
            break;
    }
    return b;
}

int furniture_wall_facing( bool row_x, bool row_y, uint8_t walls )
{
    if( !row_y ) {
        if( ( walls & face_north ) != 0 ) {
            return 0;
        }
        if( ( walls & face_south ) != 0 ) {
            return 2;
        }
    }
    if( !row_x ) {
        if( ( walls & face_east ) != 0 ) {
            return 1;
        }
        if( ( walls & face_west ) != 0 ) {
            return 3;
        }
    }
    return 0;
}

bool trim_wall_spur( uint8_t connections, uint8_t indoor_sides, uint8_t neighbour_connections )
{
    constexpr uint8_t all = face_north | face_south | face_west | face_east;
    if( connections == 0 || ( connections & ( connections - 1 ) ) != 0 ||
        ( indoor_sides & all ) != ( all & ~connections ) ) {
        return false;
    }
    const uint8_t opposite = connections == face_north ? face_south :
                             connections == face_south ? face_north :
                             connections == face_west ? face_east : face_west;
    const uint8_t corner = neighbour_connections & ~opposite;
    const bool horizontal = ( corner & ( face_west | face_east ) ) == face_west ||
                            ( corner & ( face_west | face_east ) ) == face_east;
    const bool vertical = ( corner & ( face_north | face_south ) ) == face_north ||
                          ( corner & ( face_north | face_south ) ) == face_south;
    return ( neighbour_connections & opposite ) != 0 && horizontal && vertical;
}

std::array<bool, 9> wall_footprint( uint8_t connections )
{
    // A straight wall, including its last tile, spans the whole tile. Only actual
    // turns and junctions stop an arm at the centre to avoid projecting past a corner.
    if( connections == 0 || connections == face_west || connections == face_east ) {
        connections = face_west | face_east;
    } else if( connections == face_north || connections == face_south ) {
        connections = face_north | face_south;
    }
    return {{
            false, ( connections & face_north ) != 0, false,
            ( connections & face_west ) != 0, true, ( connections & face_east ) != 0,
            false, ( connections & face_south ) != 0, false
        }};
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
            // Tops are kept even under a block: the levels above may be cut away.
            return face_top;
        case shape_kind::block:
        case shape_kind::prism:
            break;
    }
    faces |= face_top;
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
