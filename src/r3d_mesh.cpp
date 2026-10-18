#if defined(TILES)

#include "r3d_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "cata_tiles.h"
#include "debug.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "point.h"
#include "submap.h"

namespace r3d
{

namespace
{

constexpr int side = SEEX; // SEEX == SEEY
constexpr int border = side + 2;

// Light multiplier that turns a grey wall texture cell (whose average is about grey_mean, see
// make_wall_textures.py) into the given colour: the tileset's (0xAARRGGBB, alpha 0 when unknown)
// or else the preset (0xRRGGBB).
// A packed colour scaled by factor, each channel clamped to 255.
uint32_t lighten( uint32_t rgb, float factor )
{
    uint32_t out = 0;
    for( int shift = 0; shift <= 16; shift += 8 ) {
        const float c = static_cast<float>( ( rgb >> shift ) & 255u ) * factor;
        out |= static_cast<uint32_t>( std::clamp( c, 0.0f, 255.0f ) + 0.5f ) << shift;
    }
    return out;
}

uint32_t wall_tint( uint32_t tileset_color, uint32_t preset )
{
    constexpr float grey_mean = 200.0f;
    // The material's own colour, leaning a little towards the tileset's sprite: a painted wall
    // keeps a hint of its paint without turning a whole room lime green.
    constexpr float sprite_share = 0.3f;
    const bool has_sprite = ( tileset_color >> 24 ) != 0;
    uint32_t out = 0;
    for( int shift : {
             16, 8, 0
         } ) {
        const float base = static_cast<float>( ( preset >> shift ) & 255u );
        const float sprite = static_cast<float>( ( tileset_color >> shift ) & 255u );
        const float mixed = has_sprite ? base * ( 1.0f - sprite_share ) + sprite * sprite_share : base;
        const float c = mixed * 255.0f / grey_mean;
        out |= static_cast<uint32_t>( std::min( c, 255.0f ) ) << shift;
    }
    return out;
}

tile_traits traits_of( const map_data_common_t &d )
{
    tile_traits t;
    t.no_floor = d.has_flag( ter_furn_flag::TFLAG_NO_FLOOR );
    t.wall = d.has_flag( ter_furn_flag::TFLAG_WALL );
    t.transparent = d.transparent;
    t.tree = d.has_flag( ter_furn_flag::TFLAG_TREE );
    t.shrub = d.has_flag( ter_furn_flag::TFLAG_SHRUB );
    t.flat = d.has_flag( ter_furn_flag::TFLAG_FLAT );
    t.tiny = d.has_flag( ter_furn_flag::TFLAG_TINY );
    t.low = d.has_flag( ter_furn_flag::TFLAG_SHORT );
    t.flat_surface = d.has_flag( ter_furn_flag::TFLAG_FLAT_SURF );
    t.window = d.has_flag( ter_furn_flag::TFLAG_WINDOW );
    t.container = d.has_flag( ter_furn_flag::TFLAG_CONTAINER );
    // Locked and damaged doors lack DOOR but are barricadable as doors.
    t.door = d.has_flag( ter_furn_flag::TFLAG_DOOR ) || d.has_flag( "BARRICADABLE_DOOR" ) ||
             d.has_flag( "BARRICADABLE_DOOR_DAMAGED" ) || d.has_flag( "BARRICADABLE_DOOR_REINFORCED" );
    t.move_cost = d.movecost;
    return t;
}

sprite_ref to_sprite_ref( const texture_draw_data &data )
{
    if( !data.texture ) {
        return {};
    }
    float tex_w = 0.0f;
    float tex_h = 0.0f;
    if( !SDL_GetTextureSize( data.texture, &tex_w, &tex_h ) || tex_w <= 0.0f || tex_h <= 0.0f ) {
        return {};
    }
    // Half a texel in from each edge so nearest sampling never picks up a neighbouring sprite.
    const float half_u = 0.5f / tex_w;
    const float half_v = 0.5f / tex_h;
    sprite_ref ref;
    ref.sheet = data.texture;
    ref.owner = data.owner;
    ref.u0 = data.uv0.first + half_u;
    ref.v0 = data.uv0.second + half_v;
    ref.u1 = data.uv1.first - half_u;
    ref.v1 = data.uv1.second - half_v;
    ref.size_x = data.size_x;
    ref.size_y = data.size_y;
    ref.top_color = data.top_color;
    ref.body_color = data.body_color;
    ref.quarter_turns = data.quarter_turns;
    ref.facing = data.facing;
    // size is in tiles and dimensions in pixels, so a tile is dimensions / size pixels.
    const float tile_w = static_cast<float>( std::max( 1, data.dimensions.w ) ) / std::max( data.size_x,
                         0.01f );
    const float tile_h = static_cast<float>( std::max( 1, data.dimensions.h ) ) / std::max( data.size_y,
                         0.01f );
    ref.offset_x = static_cast<float>( data.offset_x ) / tile_w;
    ref.offset_y = static_cast<float>( data.offset_y ) / tile_h;
    ref.flip = data.flip;
    if( data.opaque.w > 0 && data.opaque.h > 0 && data.dimensions.w > 0 && data.dimensions.h > 0 ) {
        const float w = static_cast<float>( data.dimensions.w );
        const float h = static_cast<float>( data.dimensions.h );
        ref.opaque_x0 = static_cast<float>( data.opaque.x ) / w;
        ref.opaque_y0 = static_cast<float>( data.opaque.y ) / h;
        ref.opaque_x1 = static_cast<float>( data.opaque.x + data.opaque.w ) / w;
        ref.opaque_y1 = static_cast<float>( data.opaque.y + data.opaque.h ) / h;
    }
    ref.valid = true;
    return ref;
}

// Looks up both layers of a tile. A tile with neither falls back to the tileset's "unknown" sprite.
void resolve_layers( cata_tiles &tiles, const std::string &id, TILE_CATEGORY category,
                     tile_info &out, int subtile = -1, int rota = 0, const std::string &variant = "" )
{
    const tripoint_bub_ms origin( 0, 0, 0 );
    if( const std::optional<texture_draw_data> bg = tiles.get_sprite_draw_data( id, category, origin,
            true, subtile, rota, variant ) ) {
        out.backdrop = to_sprite_ref( *bg );
    }
    if( const std::optional<texture_draw_data> fg = tiles.get_sprite_draw_data( id, category, origin,
            false, subtile, rota, variant ) ) {
        out.sprite = to_sprite_ref( *fg );
    }
    if( !out.backdrop.valid && !out.sprite.valid ) {
        if( const std::optional<texture_draw_data> unknown = tiles.get_sprite_draw_data( "unknown",
                TILE_CATEGORY::NONE, origin ) ) {
            out.sprite = to_sprite_ref( *unknown );
        }
    }
}

shape combine( const shape &terrain, const shape &furniture )
{
    if( terrain.kind == shape_kind::block ) {
        return terrain;
    }
    if( furniture.kind == shape_kind::prism || furniture.kind == shape_kind::block ) {
        return furniture;
    }
    return terrain;
}

// Light from above and the south-west, with clearly different faces as isometric art shades
// them: that contrast is what makes boxes read as solid. top N S W E
struct point_f {
    float x;
    float y;
};

constexpr std::array<uint8_t, 5> face_shade = { 255, 165, 212, 150, 185 };

// Faces are wound clockwise as seen from outside (in the world frame, see quad_sink::add), so
// the pipeline can leave their backs out. A face meant to be seen from both sides is added
// twice, the second time turned with this.
template<typename T>
T reversed_winding( const T &quad )
{
    T out = quad;
    std::swap( out[1], out[3] );
    return out;
}

// All faces use the full sprite horizontally. Vertically the sprite spans one tile height starting
// at z_base, so a shorter box shows only the lower part of it.
// front: for a 3/4-view furniture sprite, the fraction at its bottom that shows the front; the
// top face then uses the rest. Negative for ordinary top-down sprites.
void emit_top( quad_sink &sink, const sprite_ref &s, int x, int y, float z, const light_ref &ref,
               float front = -1.0f )
{
    const float x0 = static_cast<float>( x );
    const float y0 = static_cast<float>( y );
    const corners_t c = {{ { x0, y0, z }, { x0 + 1, y0, z }, { x0 + 1, y0 + 1, z }, { x0, y0 + 1, z } }};
    // A tall sprite stands on its bottom square and overflows upwards; only that square is the
    // tile seen from above.
    float v0 = s.size_y > s.size_x ? s.v1 - ( s.v1 - s.v0 ) * s.size_x / s.size_y : s.v0;
    float v1 = s.v1;
    if( front >= 0.0f ) {
        v0 = s.v0;
        v1 = s.v1 - ( s.v1 - s.v0 ) * front;
    }
    const float ul = s.flip ? s.u1 : s.u0;
    const float ur = s.flip ? s.u0 : s.u1;
    const uvs_t base = {{ { ul, v0 }, { ur, v0 }, { ur, v1 }, { ul, v1 } }};
    // Turning the sprite clockwise shows, at each corner, the sprite corner a quarter turn back.
    uvs_t uv;
    for( size_t i = 0; i < 4; ++i ) {
        uv[i] = base[( i + 4 - static_cast<size_t>( s.quarter_turns & 3 ) ) % 4];
    }
    sink.add( s, c, uv, ref );
}

bool is_tall( const sprite_ref &s )
{
    return s.valid && s.size_y > s.size_x;
}

// Rows of a sprite shown on a side, as fractions of its height from the top (0) to the bottom
// (1). Unset (negative) means the bottom of the sprite, one tile per unit of box height.
struct sprite_band {
    float top = -1.0f;
    float bottom = -1.0f;
};

// The corners of a side face of the cell at (x0, y0): top-left, top-right, bottom-right,
// bottom-left as seen from outside the box, so sprites are never mirrored.
bool side_corners( face_bit face, float x0, float y0, float z_base, float z_top, corners_t &c )
{
    float lx = 0.0f;
    float ly = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    switch( face ) {
        case face_south:
            lx = x0;
            ly = y0 + 1;
            rx = x0 + 1;
            ry = y0 + 1;
            break;
        case face_north:
            lx = x0 + 1;
            ly = y0;
            rx = x0;
            ry = y0;
            break;
        case face_east:
            lx = x0 + 1;
            ly = y0 + 1;
            rx = x0 + 1;
            ry = y0;
            break;
        case face_west:
            lx = x0;
            ly = y0;
            rx = x0;
            ry = y0 + 1;
            break;
        default:
            return false;
    }
    c = {{ { lx, ly, z_top }, { rx, ry, z_top }, { rx, ry, z_base }, { lx, ly, z_base } }};
    return true;
}

void emit_side( quad_sink &sink, const sprite_ref &s, int x, int y, face_bit face, float z_base,
                float z_top, const light_ref &ref, float shift_x = 0.0f, float shift_y = 0.0f,
                const sprite_band &band = {} )
{
    const float x0 = static_cast<float>( x ) + shift_x;
    const float y0 = static_cast<float>( y ) + shift_y;
    const float dv = s.v1 - s.v0;
    const bool banded = band.top >= 0.0f;
    const float v_top = banded ? s.v0 + dv * band.top : s.v1 - dv * ( z_top - z_base );
    const float v_bot = banded ? s.v0 + dv * band.bottom : s.v1;
    corners_t c;
    if( !side_corners( face, x0, y0, z_base, z_top, c ) ) {
        return;
    }
    const uvs_t uv = {{ { s.u0, v_top }, { s.u1, v_top }, { s.u1, v_bot }, { s.u0, v_bot } }};
    sink.add( s, c, uv, ref );
}

// Upright panel through the middle of the cell, along x (along_x) or along y, one tile high.
// Door or window standing in the wall line: a thin slab through the middle of the cell, along
// x (along_x) or y, with the sprite on both broad faces.
void emit_pane( quad_sink &sink, const sprite_ref &s, int x, int y, bool along_x, float z_base,
                float height, const light_ref &ref, float half_thickness = wall_half_thickness,
                float half_width = 0.5f )
{
    const float cx = static_cast<float>( x ) + 0.5f;
    const float cy = static_cast<float>( y ) + 0.5f;
    const float z_top = z_base + height;
    const uvs_t uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, s.v1 }, { s.u0, s.v1 } }};
    for( const float side : {
             -half_thickness, half_thickness
             } ) {
        const corners_t c = along_x ?
corners_t{{ { cx - half_width, cy + side, z_top }, { cx + half_width, cy + side, z_top }, { cx + half_width, cy + side, z_base }, { cx - half_width, cy + side, z_base } }} :
        corners_t{{ { cx + side, cy + half_width, z_top }, { cx + side, cy - half_width, z_top }, { cx + side, cy - half_width, z_base }, { cx + side, cy + half_width, z_base } }};
        // Seen from either side of the wall line.
        sink.add( s, c, uv, ref );
        sink.add( s, reversed_winding( c ), reversed_winding( uv ), ref );
    }
    // The top edge, in the colour of the sprite's top rows.
    const float v_edge = s.v0 + ( s.v1 - s.v0 ) * 0.08f;
    const uvs_t edge_uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, v_edge }, { s.u0, v_edge } }};
    const corners_t top = along_x ?
corners_t{{ { cx - half_width, cy - half_thickness, z_top }, { cx + half_width, cy - half_thickness, z_top }, { cx + half_width, cy + half_thickness, z_top }, { cx - half_width, cy + half_thickness, z_top } }} :
    corners_t{{ { cx - half_thickness, cy - half_width, z_top }, { cx + half_thickness, cy - half_width, z_top }, { cx + half_thickness, cy + half_width, z_top }, { cx - half_thickness, cy + half_width, z_top } }};
    sink.add( s, top, edge_uv, ref );
}

// Quarter turns (front from south, clockwise) for a piece with its back to a wall: of the sides
// across its row (any side when it stands alone), the first with a wall; 0 when none has one.
template<typename IsWall>
int back_to_wall( bool row_x, bool row_y, IsWall is_wall )
{
    uint8_t walls = 0;
    if( is_wall( point( 0, -1 ) ) ) {
        walls |= face_north;
    }
    if( is_wall( point( 0, 1 ) ) ) {
        walls |= face_south;
    }
    if( is_wall( point( 1, 0 ) ) ) {
        walls |= face_east;
    }
    if( is_wall( point( -1, 0 ) ) ) {
        walls |= face_west;
    }
    return furniture_wall_facing( row_x, row_y, walls );
}

// A furniture box laid out by furniture_box_layout, mirrored like the flat view mirrors the
// sprite and turned clockwise facing quarter turns (its front from south). refs: top, north,
// south, west, east. faces says which sides neighbours leave open; a side that stops short of
// its tile edge is always drawn. joined (face bits) are the sides where the same furniture
// continues: the box reaches the tile edge there, so a row of lockers or a bench is one piece.
void emit_box( quad_sink &sink, const sprite_ref &s, int x, int y, float z0, float z1,
               const box_layout &b, uint8_t faces, const std::array<light_ref, 5> &refs,
               int facing, uint8_t joined, uint8_t wall_contacts, float slab = 0.0f, float depth_scale = 1.0f )
{
    if( !s.valid ) {
        return;
    }
    const box_layout bounds = place_furniture_box( b, s.flip, facing, joined, wall_contacts,
                              depth_scale );
    const float fx0 = bounds.x0;
    const float fx1 = bounds.x1;
    const float fy0 = bounds.y0;
    const float fy1 = bounds.y1;
    const float ox = static_cast<float>( x );
    const float oy = static_cast<float>( y );
    const float du = s.u1 - s.u0;
    const float dv = s.v1 - s.v0;
    const float u0 = s.u0 + du * b.col0;
    const float u1 = s.u0 + du * b.col1;

    // Top: the lid rows, turned like emit_top turns a whole sprite.
    if( ( faces & face_top ) != 0 ) {
        // A slab's top is the middle of the lid: the lid's edge rows are the sprite's outline
        // and shadow, which would read as a raised rim on a seat.
        // A slab's top is the middle of the lid in both directions: the lid's edge rows and
        // columns are the sprite's outline, shadow and the legs peeking out at its sides (a
        // top-down bench sprite draws them mid-tile), which would read as marks on the seat.
        const float lid_span = b.lid_bottom - b.lid_top;
        const float lv0 = s.v0 + dv * ( slab > 0.0f ? b.lid_top + lid_span * 0.2f : b.lid_top );
        const float lv1 = s.v0 + dv * ( slab > 0.0f ? b.lid_bottom - lid_span * 0.15f : b.lid_bottom );
        const float inset_u = slab > 0.0f ? ( u1 - u0 ) * 0.22f : 0.0f;
        const float ul = s.flip ? u1 - inset_u : u0 + inset_u;
        const float ur = s.flip ? u0 + inset_u : u1 - inset_u;
        const uvs_t base = {{ { ul, lv0 }, { ur, lv0 }, { ur, lv1 }, { ul, lv1 } }};
        uvs_t uv;
        for( size_t i = 0; i < 4; ++i ) {
            uv[i] = base[( i + 4 - static_cast<size_t>( s.quarter_turns & 3 ) ) % 4];
        }
        const corners_t c = {{ { ox + fx0, oy + fy0, z1 }, { ox + fx1, oy + fy0, z1 },
                { ox + fx1, oy + fy1, z1 }, { ox + fx0, oy + fy1, z1 }
            }
        };
        sink.add( s, c, uv, refs[0] );
    }

    // A slab (bench seat, table top) only has sides as thick as itself, an underside, and legs.
    const float zs = slab > 0.0f ? std::max( z1 - slab, z0 ) : z0;

    // Sides: the side rows, never turned; left/right as seen from outside. Only the front (the
    // south face of the art, turned with the thing) shows them whole; the other sides repeat a
    // thin strip from the edge of the front, so locker doors appear on the front only.
    const float sv0 = s.v0 + dv * b.side_top;
    const float sv1 = s.v0 + dv * b.side_bottom;
    const uvs_t side_uv = {{ { u0, sv0 }, { u1, sv0 }, { u1, sv1 }, { u0, sv1 } }};
    // A patch from the middle of the lid: the thing's own colour without doors or handles.
    const float lid_v0 = s.v0 + dv * b.lid_top;
    const float lid_v1 = s.v0 + dv * b.lid_bottom;
    const float pu0 = u0 + ( u1 - u0 ) * 0.3f;
    const float pu1 = u0 + ( u1 - u0 ) * 0.7f;
    const float pv0 = lid_v0 + ( lid_v1 - lid_v0 ) * 0.35f;
    const float pv1 = lid_v0 + ( lid_v1 - lid_v0 ) * 0.65f;
    const uvs_t plain_uv = {{ { pu0, pv0 }, { pu1, pv0 }, { pu1, pv1 }, { pu0, pv1 } }};
    // south, west, north, east: where the front ends up after each clockwise quarter turn
    static constexpr std::array<face_bit, 4> front_after = {{ face_south, face_west, face_north, face_east }};
    const face_bit front = front_after[static_cast<size_t>( facing & 3 )];
    constexpr float edge = 0.01f;
    const struct {
        face_bit face;
        bool short_of_edge;
        float lx, ly, rx, ry;
        size_t ref;
    } sides[] = {
        { face_north, fy0 > edge, fx1, fy0, fx0, fy0, 1 },
        { face_south, fy1 < 1.0f - edge, fx0, fy1, fx1, fy1, 2 },
        { face_west, fx0 > edge, fx0, fy0, fx0, fy1, 3 },
        { face_east, fx1 < 1.0f - edge, fx1, fy1, fx1, fy0, 4 },
    };
    for( const auto &sd : sides ) {
        if( ( faces & sd.face ) == 0 && !sd.short_of_edge ) {
            continue;
        }
        const corners_t c = {{ { ox + sd.lx, oy + sd.ly, z1 }, { ox + sd.rx, oy + sd.ry, z1 },
                { ox + sd.rx, oy + sd.ry, zs }, { ox + sd.lx, oy + sd.ly, zs }
            }
        };
        // A slab's sides are its own wood (the lid's colour), lit like any side.
        sink.add( s, c, slab > 0.0f ? plain_uv : sd.face == front ? side_uv : plain_uv, refs[sd.ref] );
    }
    if( zs <= z0 ) {
        return;
    }

    // Underside of the slab, in the shade of a side.
    const corners_t under = {{ { ox + fx0, oy + fy1, zs }, { ox + fx1, oy + fy1, zs },
            { ox + fx1, oy + fy0, zs }, { ox + fx0, oy + fy0, zs }
        }
    };
    sink.add( s, under, side_uv, refs[3] );

    // Legs: every tile of a bench or table stands on one pair of legs, one at each side
    // across its row (the row runs along x before the turn), in the middle of the tile.
    constexpr float leg = 0.1f;
    constexpr float inset = 0.05f;
    const bool across_y = ( facing & 1 ) == 0;
    const float a0 = ( ( across_y ? fx0 : fy0 ) + ( across_y ? fx1 : fy1 ) - leg ) * 0.5f;
    const float s0 = ( across_y ? fy0 : fx0 ) + inset;
    const float s1 = ( across_y ? fy1 : fx1 ) - inset - leg;
    const std::array<std::array<float, 2>, 2> legs = {{
            { a0, s0 }, { a0, s1 }
        }
    };
    for( const auto &pos : legs ) {
        const point_f l{ across_y ? pos[0] : pos[1], across_y ? pos[1] : pos[0] };
        const float lx0 = ox + l.x;
        const float ly0 = oy + l.y;
        const float lx1 = lx0 + leg;
        const float ly1 = ly0 + leg;
        const std::array<std::array<float, 4>, 4> post = {{
                { lx1, ly0, lx0, ly0 }, { lx0, ly1, lx1, ly1 }, { lx0, ly0, lx0, ly1 }, { lx1, ly1, lx1, ly0 }
            }
        };
        for( size_t i = 0; i < post.size(); ++i ) {
            const corners_t c = {{ { post[i][0], post[i][1], zs }, { post[i][2], post[i][3], zs },
                    { post[i][2], post[i][3], z0 }, { post[i][0], post[i][1], z0 }
                }
            };
            // Legs stand in the seat's shadow: darker than the sides above them.
            light_ref leg_ref = refs[i + 1];
            leg_ref.shade = static_cast<uint8_t>( leg_ref.shade * 2 / 3 );
            sink.add( s, c, side_uv, leg_ref );
        }
    }
}

// Foreground decals sit this far in front of the backdrop so the depth test lets them through.
constexpr float layer_lift = 0.004f;

// Emits the backdrop (if the tile has one) and then the foreground slightly in front of it.
void emit_top_layers( quad_sink &sink, const tile_info &t, int x, int y, float z,
                      const light_ref &ref, float front = -1.0f )
{
    if( t.backdrop.valid ) {
        emit_top( sink, t.backdrop, x, y, z, ref, front );
    }
    emit_top( sink, t.sprite, x, y, t.backdrop.valid ? z + layer_lift : z, ref, front );
}

// Height, in levels, of one repeat of a tileset sprite up a side: about one tile width at the
// usual storey height, so a brick or plank sprite keeps its scale instead of being stretched
// over the whole storey.
constexpr float side_repeat = 0.6f;

// A side textured with the tile's own sprite, repeated up the face from its base (strips of
// side_repeat; the last one shows the bottom of the next repeat). Walls that have no entry in
// the wall-side atlas, or all of them when the option is off, take their look from the tileset
// this way.
void emit_side_tiled( quad_sink &sink, const sprite_ref &s, int x, int y, face_bit face,
                      float z_base,
                      float z_top, const light_ref &ref, float shift_x, float shift_y )
{
    for( float z0 = z_base; z0 < z_top - 0.001f; z0 += side_repeat ) {
        const float z1 = std::min( z0 + side_repeat, z_top );
        const float shown = ( z1 - z0 ) / side_repeat;
        emit_side( sink, s, x, y, face, z0, z1, ref, shift_x, shift_y, sprite_band{ 1.0f - shown, 1.0f } );
    }
}

void emit_side_layers( quad_sink &sink, const tile_info &t, int x, int y, face_bit face,
                       float z_base,
                       float z_top, const light_ref &ref, const sprite_band &band = {} )
{
    const bool whole = band.top < 0.0f;
    if( t.backdrop.valid ) {
        if( whole ) {
            emit_side_tiled( sink, t.backdrop, x, y, face, z_base, z_top, ref, 0.0f, 0.0f );
        } else {
            emit_side( sink, t.backdrop, x, y, face, z_base, z_top, ref, 0.0f, 0.0f, band );
        }
    }
    // Pushing the foreground outwards means shifting the whole face by its normal.
    float dx = 0.0f;
    float dy = 0.0f;
    if( t.backdrop.valid ) {
        dx = face == face_east ? layer_lift : face == face_west ? -layer_lift : 0.0f;
        dy = face == face_south ? layer_lift : face == face_north ? -layer_lift : 0.0f;
    }
    if( whole ) {
        emit_side_tiled( sink, t.sprite, x, y, face, z_base, z_top, ref, dx, dy );
    } else {
        emit_side( sink, t.sprite, x, y, face, z_base, z_top, ref, dx, dy, band );
    }
}

// A side of a wall block: its own side texture when it has one, else the tileset sprite.
light_ref tinted( light_ref ref, uint32_t rgb )
{
    ref.tint_r = static_cast<uint8_t>( ( rgb >> 16 ) & 255u );
    ref.tint_g = static_cast<uint8_t>( ( rgb >> 8 ) & 255u );
    ref.tint_b = static_cast<uint8_t>( rgb & 255u );
    return ref;
}

void emit_wall_side( quad_sink &sink, const tile_info &t, int x, int y, face_bit face, float z_base,
                     float z_top, const light_ref &ref )
{
    if( t.side.valid ) {
        emit_side( sink, t.side, x, y, face, z_base, z_top, tinted( ref, t.side_tint ) );
    } else if( t.elevation_split >= 0.0f ) {
        // Coving, paint and skirting board read from the edge-on sprite, once over the storey.
        emit_side( sink, t.elevation, x, y, face, z_base, z_top, ref, 0.0f, 0.0f,
                   sprite_band{ t.elevation_split, 1.0f } );
    } else {
        emit_side_layers( sink, t, x, y, face, z_base, z_top, ref );
    }
}

// A portion of a full-height wall face, retaining the original texture coordinates so
// sill, lintel and jambs line up with the neighbouring wall's paint and trim.
void emit_wall_section( quad_sink &sink, const tile_info &t, int x, int y, face_bit face,
                        float bottom, float top, float left, float right, const light_ref &ref,
                        float x0 = 0.0f, float y0 = 0.0f, float x1 = 1.0f, float y1 = 1.0f )
{
    const sprite_ref &art = t.side.valid ? t.side :
                            t.elevation.valid ? t.elevation : t.main();
    const float split = t.elevation.valid ? t.elevation_split : 0.0f;
    corners_t c;
    if( !side_corners( face, x, y, bottom, top, c ) ) {
        return;
    }
    for( size_t axis = 0; axis < 2; ++axis ) {
        const float across = c[1][axis] - c[0][axis];
        c[1][axis] = c[0][axis] + across * right;
        c[0][axis] += across * left;
        c[2][axis] = c[3][axis] + across * right;
        c[3][axis] += across * left;
    }
    for( auto &corner : c ) {
        corner[0] = x + x0 + ( corner[0] - x ) * ( x1 - x0 );
        corner[1] = y + y0 + ( corner[1] - y ) * ( y1 - y0 );
    }
    const float du = art.u1 - art.u0;
    const float dv = art.v1 - art.v0;
    const float u0 = art.u0 + du * left;
    const float u1 = art.u0 + du * right;
    const float v0 = art.v0 + dv * ( split + ( 1.0f - top ) * ( 1.0f - split ) );
    const float v1 = art.v0 + dv * ( split + ( 1.0f - bottom ) * ( 1.0f - split ) );
    const uvs_t uv = {{ { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } }};
    sink.add( art, c, uv, t.side.valid ? tinted( ref, t.side_tint ) : ref );
}

void emit_wall_cap( quad_sink &sink, const tile_info &t, int x, int y, float z,
                    float x0, float y0, float x1, float y1, const light_ref &ref )
{
    sprite_ref art = t.cap.valid ? t.cap : t.elevation.valid ? t.elevation : t.main();
    if( t.elevation.valid && !t.cap.valid ) {
        const float dv = art.v1 - art.v0;
        art.v0 += dv * ( t.elevation_split + ( 1.0f - t.elevation_split ) * 0.35f );
        art.v1 = art.v0 + dv * 0.1f;
    }
    const corners_t c = {{ { x + x0, y + y0, z }, { x + x1, y + y0, z },
            { x + x1, y + y1, z }, { x + x0, y + y1, z }
        }
    };
    const uvs_t uv = {{ { art.u0, art.v0 }, { art.u1, art.v0 },
            { art.u1, art.v1 }, { art.u0, art.v1 }
        }
    };
    sink.add( art, c, uv, t.cap.valid ? tinted( ref, t.cap_tint ) : ref );
}

light_ref wall_ref( int lx, int ly, int dx, int dy, int dz, uint8_t shade );

void emit_thin_wall( quad_sink &sink, const tile_info &t, int x, int y, uint8_t connections,
                     const std::array<const tile_info *, 4> &side_materials )
{
    const std::array<bool, 9> occupied = wall_footprint( connections );
    const float edges[] = { 0.0f, 0.5f - wall_half_thickness,
                            0.5f + wall_half_thickness, 1.0f
                          };
    for( int row = 0; row < 3; ++row ) {
        for( int col = 0; col < 3; ++col ) {
            if( !occupied[row * 3 + col] ) {
                continue;
            }
            const float x0 = edges[col];
            const float y0 = edges[row];
            const float x1 = edges[col + 1];
            const float y1 = edges[row + 1];
            emit_wall_cap( sink, t, x, y, t.sh.height, x0, y0, x1, y1,
                           wall_ref( x, y, 0, 0, 0, face_shade[0] ) );
            const std::array<face_bit, 4> faces = {{ face_north, face_south, face_west, face_east }};
            const int dx[] = { 0, 0, -1, 1 };
            const int dy[] = { -1, 1, 0, 0 };
            for( size_t i = 0; i < faces.size(); ++i ) {
                const int nx = col + dx[i];
                const int ny = row + dy[i];
                const bool outside = nx < 0 || nx > 2 || ny < 0 || ny > 2;
                if( outside ? ( connections & faces[i] ) != 0 : occupied[ny * 3 + nx] ) {
                    continue;
                }
                emit_wall_section( sink, *side_materials[i], x, y, faces[i], 0.0f, t.sh.height, 0.0f, 1.0f,
                                   wall_ref( x, y, dx[i], dy[i], 0, face_shade[i + 1] ),
                                   x0, y0, x1, y1 );
            }
        }
    }
}

// How much of a 3/4-view furniture sprite is its front, for a box of the given height (in
// z-levels): taller furniture shows more front. Storeys are drawn about 1.8 tiles tall.
float front_fraction( float height )
{
    // Tall storage (lockers, fridges, bookcases) is mostly front in the art: doors and shelves
    // under a thin lid.
    constexpr float tall = 0.6f;
    if( height >= tall ) {
        return 0.78f;
    }
    const float seen = height * 1.8f;
    return std::clamp( seen / ( seen + 1.0f ), 0.25f, 0.65f );
}

light_ref make_ref( int lx, int ly, int dx, int dy, int dz, uint8_t shade )
{
    light_ref r;
    r.lx = static_cast<uint8_t>( lx );
    r.ly = static_cast<uint8_t>( ly );
    r.dx = static_cast<int8_t>( dx );
    r.dy = static_cast<int8_t>( dy );
    r.dz = static_cast<int8_t>( dz );
    r.shade = shade;
    return r;
}

light_ref wall_ref( int lx, int ly, int dx, int dy, int dz, uint8_t shade )
{
    light_ref r = make_ref( lx, ly, dx, dy, dz, shade );
    r.wall = true;
    return r;
}

} // namespace

uint32_t pack_light( uint8_t r, uint8_t g, uint8_t b )
{
    return static_cast<uint32_t>( r ) | ( static_cast<uint32_t>( g ) << 8 ) |
           ( static_cast<uint32_t>( b ) << 16 ) | ( 255u << 24 );
}

void quad_sink::add( const sprite_ref &sprite, const corners_t &corners, const uvs_t &uvs,
                     const light_ref &ref )
{
    add_colored( sprite, corners, uvs, ref, pack_light( ref.shade, ref.shade, ref.shade ) );
}

void quad_sink::add_colored( const sprite_ref &sprite, const corners_t &corners, const uvs_t &uvs,
                             const light_ref &ref, uint32_t color )
{
    add_colored( sprite, corners, uvs, ref, std::array<uint32_t, 4> {{ color, color, color, color }} );
}

void quad_sink::add_colored( const sprite_ref &sprite, const corners_t &corners, const uvs_t &uvs,
                             const light_ref &ref, const std::array<uint32_t, 4> &colors )
{
    if( !sprite.valid ) {
        return;
    }
    quad q;
    q.sheet = sprite.sheet;
    q.colors = colors;
    for( size_t i = 0; i < 4; ++i ) {
        // Tile space (x east, y south, z up) is flipped into the right-handed world frame.
        q.v[i] = vertex{ corners[i][0], -corners[i][1], corners[i][2], uvs[i][0], uvs[i][1] };
        q.r[i] = ref;
    }
    quads_.push_back( q );
}

chunk_mesh quad_sink::finish()
{
    std::stable_sort( quads_.begin(), quads_.end(), []( const quad & a, const quad & b ) {
        return a.sheet < b.sheet;
    } );
    chunk_mesh mesh;
    mesh.vertices.reserve( quads_.size() * 4 );
    mesh.light.reserve( quads_.size() * 4 );
    mesh.light_refs.reserve( quads_.size() * 4 );
    mesh.indices.reserve( quads_.size() * 6 );
    for( const quad &q : quads_ ) {
        if( mesh.batches.empty() || mesh.batches.back().sheet != q.sheet ) {
            mesh_batch b;
            b.sheet = q.sheet;
            b.first_index = static_cast<uint32_t>( mesh.indices.size() );
            mesh.batches.push_back( b );
        }
        const uint32_t base = static_cast<uint32_t>( mesh.vertices.size() );
        for( size_t i = 0; i < 4; ++i ) {
            mesh.vertices.push_back( q.v[i] );
            mesh.light_refs.push_back( q.r[i] );
            mesh.light.push_back( q.colors[i] );
        }
        for( uint32_t idx : {
                 0u, 1u, 2u, 0u, 2u, 3u
             } ) {
            mesh.indices.push_back( base + idx );
        }
        mesh.batches.back().index_count += 6;
    }
    return mesh;
}

void tile_info_cache::set_thin_walls( bool on )
{
    if( on != thin_walls_ ) {
        thin_walls_ = on;
        clear();
    }
}

void tile_info_cache::set_plaster_atlas( bool on )
{
    if( on != plaster_atlas_ ) {
        plaster_atlas_ = on;
        clear();
    }
}

void tile_info_cache::set_wall_sides( std::shared_ptr<const wall_sides> sides )
{
    wall_sides_ = std::move( sides );
    clear();
}

void tile_info_cache::clear()
{
    terrain_.clear();
    furniture_.clear();
    named_.clear();
    ground_ = {};
    ground_loaded_ = false;
}

namespace
{
// One cache entry per id and orientation; subtile is -1..7 and rota -1..3.
int orientation_key( int id, int subtile, int rota )
{
    return id * 128 + ( subtile + 1 ) * 8 + ( rota + 1 );
}
} // namespace

const tile_info &tile_info_cache::terrain( ter_id id, cata_tiles &tiles, int subtile, int rota )
{
    const int key = orientation_key( id.to_i(), subtile, rota );
    auto it = terrain_.find( key );
    if( it != terrain_.end() ) {
        return it->second;
    }
    tile_info info;
    const ter_t &t = id.obj();
    const tile_traits traits = traits_of( t );
    info.sh = classify_terrain( traits );
    info.tree = traits.tree;
    info.window = traits.window;
    info.thin_wall = traits.wall && thin_walls_;
    info.wall_connection = traits.wall || traits.window || traits.door;
    info.see_through = traits.transparent;
    if( info.sh.kind != shape_kind::empty ) {
        resolve_layers( tiles, t.id.str(), TILE_CATEGORY::TERRAIN, info, subtile, rota );
    }
    if( info.sh.kind == shape_kind::block && wall_sides_ ) {
        // The id and what it looks like, nearest first, pick the side texture.
        std::vector<std::string> chain{ t.id.str() };
        std::string like = t.looks_like;
        while( !like.empty() && chain.size() < 8 ) {
            chain.push_back( like );
            const ter_str_id next( like );
            like = next.is_valid() ? next.obj().looks_like : std::string();
        }
        const std::string name = match_wall_texture( wall_sides_->rules, chain );
        info.brick_wall = name == "brick";
        const auto found = std::find( wall_sides_->names.begin(), wall_sides_->names.end(), name );
        // A wall of a patterned material (brick, wood, logs, stone, metal...) keeps the
        // tileset's own look: its sprite is repeated up the sides and tops the wall. Painted
        // and plain walls, whose sprites only give a colour, take the atlas texture instead.
        const bool patterned_material = !name.empty() && name != "plaster" && info.sprite.valid;
        // A painted or plain wall's sprite is drawn as a front elevation: a band of framing at
        // the top, then coving, the paint and a skirting board. Read that way it is the side,
        // and the framing band tops the wall; the atlas plaster is the alternative.
        // Of the wall's multitile pieces, the east-west edge shows it edge-on: a cavity (the
        // top rows), a pale coving line, the paint and a skirting board. That piece is the
        // elevation of every side, whatever piece the tile itself is drawn with from above.
        constexpr int edge_subtile = 2; // "edge" in cata_tiles' multitile_keys
        constexpr int edge_east_west = 0;
        constexpr float cavity_rows = 0.125f;
        if( !patterned_material && !plaster_atlas_ && info.sprite.valid ) {
            info.elevation = info.sprite;
            if( const std::optional<texture_draw_data> edge = tiles.get_sprite_draw_data(
                        t.id.str(), TILE_CATEGORY::TERRAIN, tripoint_bub_ms( 0, 0, 0 ), false,
                        edge_subtile, edge_east_west ) ) {
                info.elevation = to_sprite_ref( *edge );
            }
            const sprite_ref &art = info.elevation;
            info.elevation_split = art.opaque_y0 + ( art.opaque_y1 - art.opaque_y0 ) * cavity_rows;
            // Debug aid: CATA_R3D_DEBUG_WALLS=1 logs the sprite geometry of each painted wall.
            static const bool debug_walls = std::getenv( "CATA_R3D_DEBUG_WALLS" ) != nullptr;
            if( debug_walls ) {
                DebugLog( D_INFO, DC_ALL ) << "r3d wall " << t.id.str() << " sub=" << subtile << " rot=" << rota
                                           << " uv=" << art.u0 << "," << art.v0 << "-" << art.u1 << "," << art.v1
                                           << " size=" << art.size_x << "x" << art.size_y
                                           << " opaque=" << art.opaque_x0 << "," << art.opaque_y0 << "-"
                                           << art.opaque_x1 << "," << art.opaque_y1
                                           << " backdrop=" << info.backdrop.valid << " side=" << info.side.valid;
            }
        } else if( !name.empty() && found != wall_sides_->names.end() && !patterned_material ) {
            const uv_rect uv = atlas_cell( static_cast<int>( found - wall_sides_->names.begin() ),
                                           wall_sides_->cell, wall_sides_->width, wall_sides_->height );
            if( uv.valid ) {
                info.side.sheet = wall_sides_->atlas.get();
                info.side.owner = wall_sides_->atlas;
                info.side.u0 = uv.u0;
                info.side.v0 = uv.v0;
                info.side.u1 = uv.u1;
                info.side.v1 = uv.v1;
                info.side.valid = true;
                // The cap is the middle of the texture, scaled up: an even surface in the wall's
                // colour, as isometric tilesets draw the top of a wall.
                info.cap = info.side;
                const float du = ( uv.u1 - uv.u0 ) * 0.4f;
                const float dv = ( uv.v1 - uv.v0 ) * 0.4f;
                info.cap.u0 += du;
                info.cap.u1 -= du;
                info.cap.v0 += dv;
                info.cap.v1 -= dv;
                // Colours: the tileset's own wall sprite (its front for the sides, its top for
                // the cap), else the texture's default.
                const sprite_ref &art = info.main();
                const auto fallback = wall_sides_->colors.find( name );
                const uint32_t preset = fallback != wall_sides_->colors.end() ? fallback->second :
                                        0xb4b4b4u;
                info.side_tint = wall_tint( art.body_color, preset );
                // The cap a shade lighter than the sides, so the top of a wall reads as a top
                // even where the paint is one colour.
                info.cap_tint = lighten( wall_tint( art.top_color, preset ), 1.18f );
            }
        }
    }
    return terrain_.emplace( key, info ).first->second;
}

const tile_info &tile_info_cache::named( TILE_CATEGORY category, const std::string &id,
        cata_tiles &tiles, int subtile, int rota, const std::string &variant )
{
    const std::string key = std::to_string( static_cast<int>( category ) ) + ":" +
                            std::to_string( subtile ) + ":" + std::to_string( rota ) + ":" +
                            std::to_string( id.size() ) + ":" + id + variant;
    auto it = named_.find( key );
    if( it != named_.end() ) {
        return it->second;
    }
    tile_info info;
    resolve_layers( tiles, id, category, info, subtile, rota, variant );
    return named_.emplace( key, info ).first->second;
}

const tile_info &tile_info_cache::furniture( furn_id id, cata_tiles &tiles, int subtile, int rota )
{
    const int key = orientation_key( id.to_i(), subtile, rota );
    auto it = furniture_.find( key );
    if( it != furniture_.end() ) {
        return it->second;
    }
    tile_info info;
    const furn_t &f = id.obj();
    if( f.id != furn_str_id::NULL_ID() ) {
        tile_traits traits = traits_of( f );
        // Furniture has a "move cost mod": negative means impassable, anything else walkable.
        traits.move_cost = f.movecost < 0 ? 0 : 100;
        // Notice boards keep their complete front elevation; lockers have a shallow
        // footprint. Follow artwork inheritance so variants keep the same geometry.
        std::string art_id = f.id.str();
        for( int depth = 0; depth < 8 && !art_id.empty(); ++depth ) {
            if( art_id.compare( 0, 10, "f_boulder_" ) == 0 ) {
                traits.boulder = true;
            }
            if( art_id == "f_locker" || art_id == "f_vitrified_locker" ) {
                info.depth_scale = 0.5f;
            }
            if( art_id == "f_bulletin" ) {
                traits.panel = true;
                break;
            }
            const furn_str_id candidate( art_id );
            art_id = candidate.is_valid() ? candidate.obj().looks_like : std::string();
        }
        info.sh = classify_furniture( traits );
        resolve_layers( tiles, f.id.str(), TILE_CATEGORY::FURNITURE, info, subtile, rota );
        if( info.sh.kind == shape_kind::block ) {
            // A supplied backdrop may itself have transparent holes. Keep the object's
            // artwork as detail over a solid material, including background-only tiles.
            // Use the centre tile as a continuous rock material.
            const sprite_ref detail = info.main();
            info.backdrop = traits.boulder ?
                            terrain( ter_str_id( "t_rock" ).id(), tiles, 0 ).main() : ground( tiles ).main();
            info.sprite = detail;
        }
    }
    return furniture_.emplace( key, info ).first->second;
}

const tile_info &tile_info_cache::ground( cata_tiles &tiles )
{
    if( !ground_loaded_ ) {
        resolve_layers( tiles, "t_grass", TILE_CATEGORY::TERRAIN, ground_ );
        ground_loaded_ = true;
    }
    return ground_;
}

uint64_t chunk_stamp( const tripoint_abs_sm &pos, const submap_lookup &lookup )
{
    const submap *self = lookup( pos );
    if( !self ) {
        return 0;
    }
    uint64_t stamp = self->render_stamp();
    const tripoint_rel_sm neighbours[] = { { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for( const tripoint_rel_sm &d : neighbours ) {
        const submap *n = lookup( pos + d );
        stamp = stamp * 1099511628211ull ^ ( n ? n->render_stamp() : 0ull );
    }
    return stamp;
}

namespace
{

// Queries and geometry shared by the chunk's terrain and furniture passes.
struct chunk_tile_lookup {
    const submap *self;
    const submap *north;
    const submap *south;
    const submap *west;
    const submap *east;
    tile_info_cache &cache;
    cata_tiles &tiles;
    const map &here;

    static constexpr std::array<point, 4> wall_dirs = {{
            point( 0, -1 ), point( 0, 1 ), point( -1, 0 ), point( 1, 0 )
        }
    };
    static constexpr std::array<uint8_t, 4> wall_bits = {{ face_north, face_south, face_west, face_east }};

    const tile_info *terrain_at( int lx, int ly ) const {
        const submap *sm = self;
        if( lx < 0 ) {
            sm = west;
            lx = side - 1;
        } else if( lx >= side ) {
            sm = east;
            lx = 0;
        } else if( ly < 0 ) {
            sm = north;
            ly = side - 1;
        } else if( ly >= side ) {
            sm = south;
            ly = 0;
        }
        return sm ? &cache.terrain( sm->get_ter( point_sm_ms( lx, ly ) ), tiles ) : nullptr;
    }

    furn_id furniture_at( int lx, int ly ) const {
        const submap *sm = self;
        if( lx < 0 ) {
            sm = west;
            lx = side - 1;
        } else if( lx >= side ) {
            sm = east;
            lx = 0;
        } else if( ly < 0 ) {
            sm = north;
            ly = side - 1;
        } else if( ly >= side ) {
            sm = south;
            ly = 0;
        }
        return sm ? sm->get_furn( point_sm_ms( lx, ly ) ) : furn_str_id::NULL_ID().id();
    }

    shape occupancy_at( const submap *sm, int sx, int sy ) const {
        if( !sm ) {
            return {};
        }
        const point_sm_ms p( sx, sy );
        const shape t = cache.terrain( sm->get_ter( p ), tiles ).sh;
        const shape f = cache.furniture( sm->get_furn( p ), tiles ).sh;
        return combine( t, f );
    }

    void emit_floor_under( quad_sink &sink, int lx, int ly, bool wall_along_y ) const {
        const tile_info *under = nullptr;
        const std::array<point, 4> order = wall_along_y ?
                                           std::array<point, 4> { point( -1, 0 ), point( 1, 0 ), point( 0, -1 ), point( 0, 1 ) } :
                                           std::array<point, 4> { point( 0, -1 ), point( 0, 1 ), point( -1, 0 ), point( 1, 0 ) };
        for( const point &d : order ) {
            const tile_info *n = terrain_at( lx + d.x, ly + d.y );
            if( n && n->sh.kind == shape_kind::floor && !is_tall( n->sprite ) ) {
                under = n;
                break;
            }
        }
        emit_top_layers( sink, under ? *under : cache.ground( tiles ), lx, ly, floor_height,
                         make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
    }

    const tile_info *wall_at( const tripoint_bub_ms &p ) const {
        return here.inbounds( p ) ? &cache.terrain( here.ter( p ), tiles ) : nullptr;
    }

    uint8_t connections_at( const tripoint_bub_ms &p ) const {
        uint8_t mask = 0;
        for( size_t i = 0; i < wall_dirs.size(); ++i ) {
            const point &d = wall_dirs[i];
            const tile_info *t = wall_at( p + tripoint_rel_ms( d.x, d.y, 0 ) );
            if( t && t->wall_connection ) {
                mask |= wall_bits[i];
            }
        }
        return mask;
    }

    bool trim_corner_at( const tripoint_bub_ms &p ) const {
        const tile_info *self_wall = wall_at( p );
        if( !self_wall || !self_wall->thin_wall ) {
            return false;
        }
        const uint8_t connections = connections_at( p );
        if( connections == 0 || ( connections & ( connections - 1 ) ) != 0 ) {
            return false;
        }
        uint8_t indoor = 0;
        uint8_t neighbour = 0;
        for( size_t i = 0; i < wall_dirs.size(); ++i ) {
            const point &d = wall_dirs[i];
            const tripoint_bub_ms next = p + tripoint_rel_ms( d.x, d.y, 0 );
            const tile_info *t = wall_at( next );
            if( t && t->sh.kind == shape_kind::floor &&
                here.has_flag( ter_furn_flag::TFLAG_INDOORS, next ) ) {
                indoor |= wall_bits[i];
            }
            if( ( connections & wall_bits[i] ) != 0 ) {
                neighbour = connections_at( next );
            }
        }
        return trim_wall_spur( connections, indoor, neighbour );
    }

    bool indoor_floor( const tripoint_bub_ms &p ) const {
        return here.inbounds( p ) &&
               here.has_flag( ter_furn_flag::TFLAG_INDOORS, p ) &&
               cache.terrain( here.ter( p ), tiles ).sh.kind == shape_kind::floor;
    }

    const tile_info &face_material( const tripoint_bub_ms &bub, const tile_info &wall, int dx,
                                    int dy ) const {
        if( !wall.brick_wall ) {
            return wall;
        }
        const tripoint_bub_ms adjacent = bub + tripoint_rel_ms( dx, dy, 0 );

        bool inside = indoor_floor( adjacent );
        // At an L or T junction the immediate tile can itself be wall. The
        // exposed inner corner then faces the floor diagonally beyond that wall.
        if( !inside && here.inbounds( adjacent ) &&
            cache.terrain( here.ter( adjacent ), tiles ).sh.kind == shape_kind::block ) {
            inside = indoor_floor( adjacent + tripoint_rel_ms( dy, dx, 0 ) ) ||
                     indoor_floor( adjacent - tripoint_rel_ms( dy, dx, 0 ) );
        }
        if( inside ) {
            return cache.terrain( ter_str_id( "t_wall" ).id(), tiles, 2, 0 );
        }
        return wall;
    }
};

} // namespace

chunk_mesh build_chunk_mesh( const tripoint_abs_sm &pos, const submap_lookup &lookup,
                             tile_info_cache &cache, cata_tiles &tiles )
{
    const submap *self = lookup( pos );
    if( !self ) {
        return {};
    }
    const submap *north = lookup( pos + tripoint_rel_sm( 0, -1, 0 ) );
    const submap *south = lookup( pos + tripoint_rel_sm( 0, 1, 0 ) );
    const submap *west = lookup( pos + tripoint_rel_sm( -1, 0, 0 ) );
    const submap *east = lookup( pos + tripoint_rel_sm( 1, 0, 0 ) );
    const submap *above = lookup( pos + tripoint_rel_sm( 0, 0, 1 ) );
    const submap *below = lookup( pos + tripoint_rel_sm( 0, 0, -1 ) );
    const chunk_tile_lookup chunk_tiles{ self, north, south, west, east, cache, tiles, get_map() };

    // Terrain of a tile of this level, which may lie in a horizontal neighbour (one tile out).


    // Furniture of a tile of this level, which may lie in a horizontal neighbour (one tile out).




    // Occupancy of this level including a one tile border, and of the level above.
    std::array<shape, border *border> level;
    std::array<shape, side *side> up;
    for( int ly = -1; ly <= side; ++ly ) {
        for( int lx = -1; lx <= side; ++lx ) {
            const submap *sm = self;
            int sx = lx;
            int sy = ly;
            const bool out_x = lx < 0 || lx >= side;
            const bool out_y = ly < 0 || ly >= side;
            shape sh;
            if( out_x && out_y ) {
                sh = {}; // corners never matter: faces only look at the four neighbours
            } else {
                if( lx < 0 ) {
                    sm = west;
                    sx = side - 1;
                } else if( lx >= side ) {
                    sm = east;
                    sx = 0;
                }
                if( ly < 0 ) {
                    sm = north;
                    sy = side - 1;
                } else if( ly >= side ) {
                    sm = south;
                    sy = 0;
                }
                sh = chunk_tiles.occupancy_at( sm, sx, sy );
            }
            level[static_cast<size_t>( ( ly + 1 ) * border + ( lx + 1 ) )] = sh;
        }
    }
    for( int ly = 0; ly < side; ++ly ) {
        for( int lx = 0; lx < side; ++lx ) {
            up[static_cast<size_t>( ly * side + lx )] = chunk_tiles.occupancy_at( above, lx, ly );
        }
    }
    auto level_at = [ &level ]( int lx, int ly ) -> const shape & {
        return level[static_cast<size_t>( ( ly + 1 ) * border + ( lx + 1 ) )];
    };

    // Chunks are only built for submaps in the reality bubble, so the map knows every tile here.
    const map &here = get_map();
    const tripoint_abs_ms chunk_origin = project_to<coords::ms>( pos );
    // Some mapgen corners include a one-tile inward buttress. A thin wall needs
    // just the connected outside corner, rather than a spur jutting into the room.





    quad_sink sink;
    std::vector<upright_sprite> mesh_uprights;
    // Floor for a tile that stands in a wall line (pane, see-through block): a neighbour's floor
    // on an open side (inside or out), else the ground.


    for( int ly = 0; ly < side; ++ly ) {
        for( int lx = 0; lx < side; ++lx ) {
            const point_sm_ms p( lx, ly );
            // Connected sprites (walls, benches, counters) as the flat view orients them.
            const tripoint_bub_ms bub = here.get_bub( chunk_origin + tripoint_rel_ms( lx, ly, 0 ) );
            int ter_subtile = -1;
            int ter_rota = 0;
            int furn_subtile = -1;
            int furn_rota = 0;
            tiles.get_orientation( bub, false, ter_subtile, ter_rota );
            if( self->get_furn( p ) != furn_str_id::NULL_ID() ) {
                tiles.get_orientation( bub, true, furn_subtile, furn_rota );
            }
            const tile_info &terrain = cache.terrain( self->get_ter( p ), tiles, ter_subtile, ter_rota );
            const tile_info &furn = cache.furniture( self->get_furn( p ), tiles, furn_subtile, furn_rota );
            neighbourhood nb;
            nb.up = up[static_cast<size_t>( ly * side + lx )];
            nb.north = level_at( lx, ly - 1 );
            nb.south = level_at( lx, ly + 1 );
            nb.west = level_at( lx - 1, ly );
            nb.east = level_at( lx + 1, ly );

            // The gameplay outside cache includes a one-tile sheltered border. Read the
            // actual floor's INDOORS flag instead, so the outside of an exterior wall
            // remains brick even beside a sheltered pavement or doorway.

            const shape &ts = terrain.sh;
            // A treetop is drawn by the tree's own sprite on the level below.
            if( ts.kind == shape_kind::floor && below &&
                cache.terrain( below->get_ter( p ), tiles ).tree ) {
                continue;
            }
            // Every side is built, even one a neighbour hides: the cutaway around the player
            // removes whole tiles, and the neighbour's side must then be there to show.
            constexpr uint8_t all_sides = face_north | face_south | face_west | face_east;
            const uint8_t faces = visible_faces( ts, nb ) |
                                  ( ts.kind == shape_kind::block ? all_sides : 0 );
            switch( ts.kind ) {
                case shape_kind::floor:
                    if( ( faces & face_top ) != 0 ) {
                        light_ref floor_ref = make_ref( lx, ly, 0, 0, 0, face_shade[0] );
                        floor_ref.floor = true;
                        // Tall sprites (long grass) are often transparent around what stands on
                        // the tile, so the ground goes underneath unless they bring a backdrop.
                        if( is_tall( terrain.sprite ) && !terrain.backdrop.valid ) {
                            emit_top_layers( sink, cache.ground( tiles ), lx, ly, ts.height, floor_ref );
                            emit_top( sink, terrain.sprite, lx, ly, ts.height + layer_lift, floor_ref );
                        } else {
                            emit_top_layers( sink, terrain, lx, ly, ts.height, floor_ref );
                        }
                    }
                    break;
                case shape_kind::block:
                    if( terrain.thin_wall ) {
                        if( chunk_tiles.trim_corner_at( bub ) ) {
                            chunk_tiles.emit_floor_under( sink, lx, ly, false );
                            break;
                        }
                        const std::array<point, 4> dirs = {{
                                point( 0, -1 ), point( 0, 1 ), point( -1, 0 ), point( 1, 0 )
                            }
                        };
                        const std::array<face_bit, 4> joins = {{
                                face_north, face_south, face_west, face_east
                            }
                        };
                        uint8_t connections = 0;
                        for( size_t i = 0; i < dirs.size(); ++i ) {
                            const tile_info *n = chunk_tiles.terrain_at( lx + dirs[i].x, ly + dirs[i].y );
                            if( n && n->wall_connection &&
                                !chunk_tiles.trim_corner_at( bub + tripoint_rel_ms( dirs[i].x, dirs[i].y, 0 ) ) ) {
                                connections |= joins[i];
                            }
                        }
                        chunk_tiles.emit_floor_under( sink, lx, ly, ( connections & ( face_north | face_south ) ) != 0 );
                        const std::array<const tile_info *, 4> materials = {{
                                &chunk_tiles.face_material( bub, terrain, 0, -1 ), &chunk_tiles.face_material( bub, terrain, 0, 1 ),
                                &chunk_tiles.face_material( bub, terrain, -1, 0 ), &chunk_tiles.face_material( bub, terrain, 1, 0 )
                            }
                        };
                        emit_thin_wall( sink, terrain, lx, ly, connections, materials );
                        break;
                    }
                    // Every wall stands on a floor: a see-through block (fence, bars, a sparse
                    // wall sprite) would otherwise show the level below through its holes, and
                    // the cutaway around the player would show a void where a wall was.
                    chunk_tiles.emit_floor_under( sink, lx, ly, nb.north.kind == shape_kind::block ||
                                                  nb.south.kind == shape_kind::block );
                    if( ( faces & face_top ) != 0 ) {
                        if( terrain.cap.valid ) {
                            emit_top( sink, terrain.cap, lx, ly, ts.height,
                                      tinted( wall_ref( lx, ly, 0, 0, 0, face_shade[0] ), terrain.cap_tint ) );
                        } else if( terrain.elevation_split >= 0.0f ) {
                            // A plain cap in the paint: a patch from the middle of the
                            // elevation's painted rows, scaled over the top.
                            sprite_ref patch = terrain.elevation;
                            const float dv = patch.v1 - patch.v0;
                            const float du = patch.u1 - patch.u0;
                            const float paint0 = terrain.elevation_split;
                            patch.v0 = terrain.elevation.v0 + dv * ( paint0 + ( 1.0f - paint0 ) * 0.35f );
                            patch.v1 = terrain.elevation.v0 + dv * ( paint0 + ( 1.0f - paint0 ) * 0.6f );
                            patch.u0 += du * 0.2f;
                            patch.u1 -= du * 0.2f;
                            patch.size_y = patch.size_x;
                            patch.quarter_turns = 0;
                            patch.flip = false;
                            emit_top( sink, patch, lx, ly, ts.height,
                                      wall_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                        } else {
                            emit_top_layers( sink, terrain, lx, ly, ts.height,
                                             wall_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                        }
                    }
                    if( ( faces & face_north ) != 0 ) {
                        emit_wall_side( sink, chunk_tiles.face_material( bub, terrain, 0, -1 ), lx, ly, face_north, 0.0f,
                                        ts.height,
                                        wall_ref( lx, ly, 0, -1, 0, face_shade[1] ) );
                    }
                    if( ( faces & face_south ) != 0 ) {
                        emit_wall_side( sink, chunk_tiles.face_material( bub, terrain, 0, 1 ), lx, ly, face_south, 0.0f,
                                        ts.height,
                                        wall_ref( lx, ly, 0, 1, 0, face_shade[2] ) );
                    }
                    if( ( faces & face_west ) != 0 ) {
                        emit_wall_side( sink, chunk_tiles.face_material( bub, terrain, -1, 0 ), lx, ly, face_west, 0.0f,
                                        ts.height,
                                        wall_ref( lx, ly, -1, 0, 0, face_shade[3] ) );
                    }
                    if( ( faces & face_east ) != 0 ) {
                        emit_wall_side( sink, chunk_tiles.face_material( bub, terrain, 1, 0 ), lx, ly, face_east, 0.0f,
                                        ts.height,
                                        wall_ref( lx, ly, 1, 0, 0, face_shade[4] ) );
                    }
                    break;
                case shape_kind::upright:
                    // Trees and shrubs: ground here, the sprite itself is drawn per frame.
                    emit_top_layers( sink, cache.ground( tiles ), lx, ly, floor_height,
                                     make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                    if( ts.ground_backdrop && terrain.backdrop.valid ) {
                        emit_top( sink, terrain.backdrop, lx, ly, floor_height + layer_lift,
                                  make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                    }
                    mesh_uprights.push_back( { static_cast<uint8_t>( lx ), static_cast<uint8_t>( ly ),
                                               floor_height, &terrain } );
                    break;
                case shape_kind::pane: {
                    // The frame stands along the wall line over the floor of its open sides.
                    const bool wall_along_y = nb.north.kind == shape_kind::block ||
                                              nb.south.kind == shape_kind::block;
                    chunk_tiles.emit_floor_under( sink, lx, ly, wall_along_y );
                    if( terrain.window ) {
                        // Carry the neighbouring wall across the sill and lintel. The glass
                        // occupies an opening rather than replacing an entire storey.
                        const tile_info *surround = nullptr;
                        const std::array<point, 4> neighbours = {{
                                point( -1, 0 ), point( 1, 0 ), point( 0, -1 ), point( 0, 1 )
                            }
                        };
                        for( const point &d : neighbours ) {
                            const tile_info *candidate = chunk_tiles.terrain_at( lx + d.x, ly + d.y );
                            if( candidate && candidate->sh.kind == shape_kind::block ) {
                                surround = candidate;
                                break;
                            }
                        }
                        if( !surround ) {
                            surround = &cache.terrain( ter_str_id( "t_wall" ).id(), tiles, 2, 0 );
                        }
                        const float half_width = cache.wall_half_width();
                        constexpr float sill = 0.35f;
                        constexpr float lintel = 0.9f;
                        const float x0 = wall_along_y ? 0.5f - half_width : 0.0f;
                        const float x1 = wall_along_y ? 0.5f + half_width : 1.0f;
                        const float y0 = wall_along_y ? 0.0f : 0.5f - half_width;
                        const float y1 = wall_along_y ? 1.0f : 0.5f + half_width;
                        for( const face_bit face : {
                                 face_north, face_south, face_west, face_east
                             } ) {
                            const bool broad = wall_along_y ? ( face == face_west || face == face_east ) :
                                               ( face == face_north || face == face_south );
                            if( broad ) {
                                const int dx = face == face_west ? -1 : face == face_east ? 1 : 0;
                                const int dy = face == face_north ? -1 : face == face_south ? 1 : 0;
                                const tile_info &material = chunk_tiles.face_material( bub, *surround, dx, dy );
                                const light_ref ref = wall_ref( lx, ly, 0, 0, 0, 230 );
                                emit_wall_section( sink, material, lx, ly, face,
                                                   0.0f, sill, 0.0f, 1.0f, ref, x0, y0, x1, y1 );
                                emit_wall_section( sink, material, lx, ly, face,
                                                   lintel, ts.height, 0.0f, 1.0f, ref, x0, y0, x1, y1 );
                                emit_wall_section( sink, material, lx, ly, face,
                                                   sill, lintel, 0.0f, 0.1f, ref, x0, y0, x1, y1 );
                                emit_wall_section( sink, material, lx, ly, face,
                                                   sill, lintel, 0.9f, 1.0f, ref, x0, y0, x1, y1 );
                            }
                        }
                        emit_wall_cap( sink, *surround, lx, ly, sill, x0, y0, x1, y1,
                                       wall_ref( lx, ly, 0, 0, 0, 230 ) );
                        emit_wall_cap( sink, *surround, lx, ly, ts.height, x0, y0, x1, y1,
                                       wall_ref( lx, ly, 0, 0, 0, 230 ) );
                        if( terrain.backdrop.valid ) {
                            emit_pane( sink, terrain.backdrop, lx, ly, !wall_along_y, sill,
                                       lintel - sill, make_ref( lx, ly, 0, 0, 0, 230 ), half_width + 0.001f, 0.4f );
                        }
                        emit_pane( sink, terrain.sprite, lx, ly, !wall_along_y, sill,
                                   lintel - sill, make_ref( lx, ly, 0, 0, 0, 230 ), half_width + 0.002f, 0.4f );
                        break;
                    }
                    if( terrain.backdrop.valid ) {
                        emit_pane( sink, terrain.backdrop, lx, ly, !wall_along_y, floor_height,
                                   ts.height - floor_height, make_ref( lx, ly, 0, 0, 0, 230 ) );
                    }
                    emit_pane( sink, terrain.sprite, lx, ly, !wall_along_y, floor_height,
                               ts.height - floor_height, make_ref( lx, ly, 0, 0, 0, 230 ) );
                    break;
                }
                case shape_kind::empty:
                case shape_kind::prism:
                    break;
            }

            // Furniture stands on the floor; it is skipped inside walls and under trees.
            const shape &fs = furn.sh;
            if( ts.kind != shape_kind::floor || fs.kind == shape_kind::empty ) {
                continue;
            }
            if( fs.kind == shape_kind::upright ) {
                mesh_uprights.push_back( { static_cast<uint8_t>( lx ), static_cast<uint8_t>( ly ),
                                           ts.height, &furn } );
                continue;
            }
            if( fs.kind == shape_kind::pane ) {
                const bool along_y = ( chunk_tiles.terrain_at( lx - 1, ly ) &&
                                       chunk_tiles.terrain_at( lx - 1, ly )->sh.kind == shape_kind::block ) ||
                                     ( chunk_tiles.terrain_at( lx + 1, ly ) &&
                                       chunk_tiles.terrain_at( lx + 1, ly )->sh.kind == shape_kind::block );
                emit_pane( sink, furn.main(), lx, ly, !along_y, ts.height, fs.height,
                           make_ref( lx, ly, 0, 0, 0, 230 ) );
                continue;
            }
            if( fs.kind == shape_kind::floor ) {
                emit_top_layers( sink, furn, lx, ly, fs.height,
                                 make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                continue;
            }
            if( fs.kind == shape_kind::block ) {
                // Full-volume natural objects keep their complete texture on each face;
                // there is no cabinet lid split, wall-facing placement or bench underside.
                emit_top_layers( sink, furn, lx, ly, fs.height,
                                 make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                if( fs.height > cutaway_stub_height ) {
                    // The cutaway can remove the top near the player. Close the remaining
                    // volume at its cut height, as walls do, instead of exposing a hollow box.
                    emit_top( sink, furn.backdrop.valid ? furn.backdrop : furn.main(), lx, ly,
                              cutaway_stub_height, make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                }
                const std::array<face_bit, 4> sides = {{ face_north, face_south, face_west, face_east }};
                const std::array<point, 4> offsets = {{
                        point( 0, -1 ), point( 0, 1 ), point( -1, 0 ), point( 1, 0 )
                    }
                };
                for( size_t i = 0; i < sides.size(); ++i ) {
                    emit_side_layers( sink, furn, lx, ly, sides[i], ts.height, fs.height,
                                      make_ref( lx, ly, offsets[i].x, offsets[i].y, 0, face_shade[i + 1] ),
                                      sprite_band{ 0.0f, 1.0f } );
                }
                continue;
            }
            // A box sized to the sprite's opaque part: lid rows on top, the front rows (tall
            // pieces like lockers) or the lid's front edge (low ones like benches, whose front
            // rows are mostly legs and shadow) on the sides.
            const sprite_ref &spr = furn.main();
            constexpr float low_box = 0.6f;
            box_layout layout = furniture_box_layout( spr.opaque_x0, spr.opaque_y0, spr.opaque_x1,
                                spr.opaque_y1, front_fraction( fs.height - ts.height ), fs.height < low_box );
            // The same furniture next door: a row of lockers, a long bench.
            const furn_id here_furn = self->get_furn( p );
            const std::array<point, 4> dirs = {{ point( 0, -1 ), point( 0, 1 ), point( -1, 0 ), point( 1, 0 ) }};
            const std::array<face_bit, 4> dir_faces = {{ face_north, face_south, face_west, face_east }};
            uint8_t joined = 0;
            for( size_t i = 0; i < dirs.size(); ++i ) {
                if( chunk_tiles.furniture_at( lx + dirs[i].x, ly + dirs[i].y ) == here_furn ) {
                    joined |= dir_faces[i];
                }
            }
            const bool row_x = ( joined & ( face_west | face_east ) ) != 0;
            const bool row_y = !row_x && ( joined & ( face_north | face_south ) ) != 0;
            // A connected sprite's rotation says how it joins its neighbours, not where it faces.
            int facing = furn_subtile >= 0 ? 0 : spr.facing;
            if( fs.slab > 0.0f ) {
                // A bench or table runs along its row (x before the turn): a seat about 0.6 tile
                // wide across the middle of the tile, the length of the tile when it continues
                // next door, a little shorter when it stands alone.
                constexpr float seat = 0.65f;
                constexpr float lone = 0.7f;
                const bool continues = ( joined & ( face_west | face_east | face_north | face_south ) ) != 0;
                layout.y0 = ( 1.0f - seat ) * 0.5f;
                layout.y1 = layout.y0 + seat;
                layout.x0 = continues ? 0.0f : ( 1.0f - lone ) * 0.5f;
                layout.x1 = continues ? 1.0f : layout.x0 + lone;
                if( row_x || row_y ) {
                    facing = row_y ? 1 : 0;
                }
            } else if( facing == 0 || furn.depth_scale < 1.0f ) {
                // Lockers and cabinets stand with their back to a wall, their row along it.
                facing = back_to_wall( row_x, row_y, [ &chunk_tiles, lx, ly, &bub ]( const point & d ) {
                    const tile_info *t = chunk_tiles.terrain_at( lx + d.x, ly + d.y );
                    return t && t->wall_connection && t->sh.kind == shape_kind::block &&
                           !chunk_tiles.trim_corner_at( bub + tripoint_rel_ms( d.x, d.y, 0 ) );
                } );
            }
            const std::array<light_ref, 5> refs = {{
                    make_ref( lx, ly, 0, 0, 0, face_shade[0] ), make_ref( lx, ly, 0, -1, 0, face_shade[1] ),
                    make_ref( lx, ly, 0, 1, 0, face_shade[2] ), make_ref( lx, ly, -1, 0, 0, face_shade[3] ),
                    make_ref( lx, ly, 1, 0, 0, face_shade[4] )
                }
            };
            // A slab (bench, table) sits below the cutaway, so it needs no hidden sides; the
            // faces between its joined tiles would only fight with its top along the seams.
            // Thin walls leave space between their face and the old tile boundary. Extend
            // solid cabinetry into that space, keeping its front and row joins in place.
            uint8_t wall_contacts = 0;
            if( fs.slab == 0.0f ) {
                for( size_t i = 0; i < dirs.size(); ++i ) {
                    const tile_info *t = chunk_tiles.terrain_at( lx + dirs[i].x, ly + dirs[i].y );
                    if( t && t->thin_wall && t->sh.kind == shape_kind::block &&
                        !chunk_tiles.trim_corner_at( bub + tripoint_rel_ms( dirs[i].x, dirs[i].y, 0 ) ) ) {
                        wall_contacts |= dir_faces[i];
                    }
                }
            }
            const uint8_t box_faces = visible_faces( fs, nb ) | ( fs.slab > 0.0f ? 0 : all_sides );
            emit_box( sink, spr, lx, ly, ts.height, fs.height, layout, box_faces, refs, facing, joined,
                      wall_contacts, fs.slab, furn.depth_scale );

        }
    }
    chunk_mesh mesh = sink.finish();
    mesh.uprights = std::move( mesh_uprights );
    return mesh;
}

} // namespace r3d

#endif // defined(TILES)
