#if defined(TILES)

#include "r3d_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "cata_tiles.h"
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
    ref.quarter_turns = data.quarter_turns;
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
                     tile_info &out, int subtile = -1, int rota = 0 )
{
    const tripoint_bub_ms origin( 0, 0, 0 );
    if( const std::optional<texture_draw_data> bg = tiles.get_sprite_draw_data( id, category, origin,
            true, subtile, rota ) ) {
        out.backdrop = to_sprite_ref( *bg );
    }
    if( const std::optional<texture_draw_data> fg = tiles.get_sprite_draw_data( id, category, origin,
            false, subtile, rota ) ) {
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
    if( furniture.kind == shape_kind::prism ) {
        return furniture;
    }
    return terrain;
}

constexpr std::array<uint8_t, 5> face_shade = { 255, 178, 204, 153, 191 }; // top N S W E

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
    // left/right are as seen from outside the box, so sprites are never mirrored.
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
            return;
    }
    const corners_t c = {{ { lx, ly, z_top }, { rx, ry, z_top }, { rx, ry, z_base }, { lx, ly, z_base } }};
    const uvs_t uv = {{ { s.u0, v_top }, { s.u1, v_top }, { s.u1, v_bot }, { s.u0, v_bot } }};
    sink.add( s, c, uv, ref );
}

// Upright panel through the middle of the cell, along x (along_x) or along y, one tile high.
// Door or window standing in the wall line: a thin slab through the middle of the cell, along
// x (along_x) or y, with the sprite on both broad faces.
void emit_pane( quad_sink &sink, const sprite_ref &s, int x, int y, bool along_x, float z_base,
                float height, const light_ref &ref )
{
    constexpr float half_thickness = 0.08f;
    const float cx = static_cast<float>( x ) + 0.5f;
    const float cy = static_cast<float>( y ) + 0.5f;
    const float z_top = z_base + height;
    const uvs_t uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, s.v1 }, { s.u0, s.v1 } }};
    for( const float side : { -half_thickness, half_thickness } ) {
        const corners_t c = along_x ?
        corners_t{{ { cx - 0.5f, cy + side, z_top }, { cx + 0.5f, cy + side, z_top }, { cx + 0.5f, cy + side, z_base }, { cx - 0.5f, cy + side, z_base } }} :
        corners_t{{ { cx + side, cy + 0.5f, z_top }, { cx + side, cy - 0.5f, z_top }, { cx + side, cy - 0.5f, z_base }, { cx + side, cy + 0.5f, z_base } }};
        sink.add( s, c, uv, ref );
    }
    // The top edge, in the colour of the sprite's top rows.
    const float v_edge = s.v0 + ( s.v1 - s.v0 ) * 0.08f;
    const uvs_t edge_uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, v_edge }, { s.u0, v_edge } }};
    const corners_t top = along_x ?
    corners_t{{ { cx - 0.5f, cy - half_thickness, z_top }, { cx + 0.5f, cy - half_thickness, z_top }, { cx + 0.5f, cy + half_thickness, z_top }, { cx - 0.5f, cy + half_thickness, z_top } }} :
    corners_t{{ { cx - half_thickness, cy - 0.5f, z_top }, { cx + half_thickness, cy - 0.5f, z_top }, { cx + half_thickness, cy + 0.5f, z_top }, { cx - half_thickness, cy + 0.5f, z_top } }};
    sink.add( s, top, edge_uv, ref );
}

// A furniture box laid out by furniture_box_layout, turned and mirrored like the flat view turns
// the sprite. refs: top, north, south, west, east. faces says which sides neighbours leave open;
// a side that stops short of its tile edge is always drawn.
void emit_box( quad_sink &sink, const sprite_ref &s, int x, int y, float z0, float z1,
               const box_layout &b, uint8_t faces, const std::array<light_ref, 5> &refs,
               float slab = 0.0f )
{
    if( !s.valid ) {
        return;
    }
    // Footprint in the cell, mirrored and then turned clockwise (u east, v south).
    float fx0 = s.flip ? 1.0f - b.x1 : b.x0;
    float fx1 = s.flip ? 1.0f - b.x0 : b.x1;
    float fy0 = b.y0;
    float fy1 = b.y1;
    for( int t = 0; t < ( s.quarter_turns & 3 ); ++t ) {
        const float nx0 = 1.0f - fy1;
        const float nx1 = 1.0f - fy0;
        fy0 = fx0;
        fy1 = fx1;
        fx0 = nx0;
        fx1 = nx1;
    }
    const float ox = static_cast<float>( x );
    const float oy = static_cast<float>( y );
    const float du = s.u1 - s.u0;
    const float dv = s.v1 - s.v0;
    const float u0 = s.u0 + du * b.col0;
    const float u1 = s.u0 + du * b.col1;

    // Top: the lid rows, turned like emit_top turns a whole sprite.
    if( ( faces & face_top ) != 0 ) {
        const float lv0 = s.v0 + dv * b.lid_top;
        const float lv1 = s.v0 + dv * b.lid_bottom;
        const float ul = s.flip ? u1 : u0;
        const float ur = s.flip ? u0 : u1;
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

    // Sides: the side rows, never turned; left/right as seen from outside.
    const float sv0 = s.v0 + dv * b.side_top;
    const float sv1 = s.v0 + dv * b.side_bottom;
    const uvs_t side_uv = {{ { u0, sv0 }, { u1, sv0 }, { u1, sv1 }, { u0, sv1 } }};
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
        sink.add( s, c, side_uv, refs[sd.ref] );
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

    // Legs at the free corners: a corner between two sides that join a neighbouring piece (a
    // long connected bench) has none, so only the ends of a row stand on legs.
    constexpr float leg = 0.07f;
    constexpr float inset = 0.05f;
    const bool open_n = ( faces & face_north ) != 0 || fy0 > edge;
    const bool open_s = ( faces & face_south ) != 0 || fy1 < 1.0f - edge;
    const bool open_w = ( faces & face_west ) != 0 || fx0 > edge;
    const bool open_e = ( faces & face_east ) != 0 || fx1 < 1.0f - edge;
    const struct {
        bool show;
        float x;
        float y;
    } legs[] = {
        { open_n && open_w, fx0 + inset, fy0 + inset },
        { open_n && open_e, fx1 - inset - leg, fy0 + inset },
        { open_s && open_w, fx0 + inset, fy1 - inset - leg },
        { open_s && open_e, fx1 - inset - leg, fy1 - inset - leg },
    };
    for( const auto &l : legs ) {
        if( !l.show ) {
            continue;
        }
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
            sink.add( s, c, side_uv, refs[i + 1] );
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

void emit_side_layers( quad_sink &sink, const tile_info &t, int x, int y, face_bit face, float z_base,
                       float z_top, const light_ref &ref, const sprite_band &band = {} )
{
    if( t.backdrop.valid ) {
        emit_side( sink, t.backdrop, x, y, face, z_base, z_top, ref, 0.0f, 0.0f, band );
    }
    // Pushing the foreground outwards means shifting the whole face by its normal.
    float dx = 0.0f;
    float dy = 0.0f;
    if( t.backdrop.valid ) {
        dx = face == face_east ? layer_lift : face == face_west ? -layer_lift : 0.0f;
        dy = face == face_south ? layer_lift : face == face_north ? -layer_lift : 0.0f;
    }
    emit_side( sink, t.sprite, x, y, face, z_base, z_top, ref, dx, dy, band );
}

// A side of a wall block: its own side texture when it has one, else the tileset sprite.
void emit_wall_side( quad_sink &sink, const tile_info &t, int x, int y, face_bit face, float z_base,
                     float z_top, const light_ref &ref )
{
    if( t.side.valid ) {
        emit_side( sink, t.side, x, y, face, z_base, z_top, ref );
    } else {
        emit_side_layers( sink, t, x, y, face, z_base, z_top, ref );
    }
}

// How much of a 3/4-view furniture sprite is its front, for a box of the given height (in
// z-levels): taller furniture shows more front. Storeys are drawn about 1.6 tiles tall.
float front_fraction( float height )
{
    const float seen = height * 1.6f;
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
    if( !sprite.valid ) {
        return;
    }
    quad q;
    q.sheet = sprite.sheet;
    q.color = color;
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
            mesh.light.push_back( q.color );
        }
        for( uint32_t idx : { 0u, 1u, 2u, 0u, 2u, 3u } ) {
            mesh.indices.push_back( base + idx );
        }
        mesh.batches.back().index_count += 6;
    }
    return mesh;
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
        const auto found = std::find( wall_sides_->names.begin(), wall_sides_->names.end(), name );
        if( !name.empty() && found != wall_sides_->names.end() ) {
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
            }
        }
    }
    return terrain_.emplace( key, info ).first->second;
}

const tile_info &tile_info_cache::named( TILE_CATEGORY category, const std::string &id,
        cata_tiles &tiles )
{
    const std::string key = std::to_string( static_cast<int>( category ) ) + ":" + id;
    auto it = named_.find( key );
    if( it != named_.end() ) {
        return it->second;
    }
    tile_info info;
    resolve_layers( tiles, id, category, info );
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
        info.sh = classify_furniture( traits );
        resolve_layers( tiles, f.id.str(), TILE_CATEGORY::FURNITURE, info, subtile, rota );
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

    // Terrain of a tile of this level, which may lie in a horizontal neighbour (one tile out).
    auto terrain_at = [&]( int lx, int ly ) -> const tile_info * {
        const submap *sm = self;
        if( lx < 0 )
        {
            sm = west;
            lx = side - 1;
        } else if( lx >= side )
        {
            sm = east;
            lx = 0;
        } else if( ly < 0 )
        {
            sm = north;
            ly = side - 1;
        } else if( ly >= side )
        {
            sm = south;
            ly = 0;
        }
        return sm ? &cache.terrain( sm->get_ter( point_sm_ms( lx, ly ) ), tiles ) : nullptr;
    };

    auto occupancy_at = [&]( const submap * sm, int sx, int sy ) -> shape {
        if( !sm )
        {
            return {};
        }
        const point_sm_ms p( sx, sy );
        const shape t = cache.terrain( sm->get_ter( p ), tiles ).sh;
        const shape f = cache.furniture( sm->get_furn( p ), tiles ).sh;
        return combine( t, f );
    };

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
                sh = occupancy_at( sm, sx, sy );
            }
            level[static_cast<size_t>( ( ly + 1 ) * border + ( lx + 1 ) )] = sh;
        }
    }
    for( int ly = 0; ly < side; ++ly ) {
        for( int lx = 0; lx < side; ++lx ) {
            up[static_cast<size_t>( ly * side + lx )] = occupancy_at( above, lx, ly );
        }
    }
    auto level_at = [&]( int lx, int ly ) -> const shape & {
        return level[static_cast<size_t>( ( ly + 1 ) * border + ( lx + 1 ) )];
    };

    // Chunks are only built for submaps in the reality bubble, so the map knows every tile here.
    const map &here = get_map();
    const tripoint_abs_ms chunk_origin = project_to<coords::ms>( pos );
    quad_sink sink;
    std::vector<upright_sprite> mesh_uprights;
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

            const shape &ts = terrain.sh;
            // A treetop is drawn by the tree's own sprite on the level below.
            if( ts.kind == shape_kind::floor && below &&
                cache.terrain( below->get_ter( p ), tiles ).tree ) {
                continue;
            }
            const uint8_t faces = visible_faces( ts, nb );
            switch( ts.kind ) {
                case shape_kind::floor:
                    if( ( faces & face_top ) != 0 ) {
                        // Tall sprites (long grass) are often transparent around what stands on
                        // the tile, so the ground goes underneath unless they bring a backdrop.
                        if( is_tall( terrain.sprite ) && !terrain.backdrop.valid ) {
                            emit_top_layers( sink, cache.ground( tiles ), lx, ly, ts.height,
                                             make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                            emit_top( sink, terrain.sprite, lx, ly, ts.height + layer_lift,
                                      make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                        } else {
                            emit_top_layers( sink, terrain, lx, ly, ts.height,
                                             make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                        }
                    }
                    break;
                case shape_kind::block:
                    if( ( faces & face_top ) != 0 ) {
                        emit_top_layers( sink, terrain, lx, ly, ts.height,
                                        make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                    }
                    if( ( faces & face_north ) != 0 ) {
                        emit_wall_side( sink, terrain, lx, ly, face_north, 0.0f, ts.height,
                                         make_ref( lx, ly, 0, -1, 0, face_shade[1] ) );
                    }
                    if( ( faces & face_south ) != 0 ) {
                        emit_wall_side( sink, terrain, lx, ly, face_south, 0.0f, ts.height,
                                         make_ref( lx, ly, 0, 1, 0, face_shade[2] ) );
                    }
                    if( ( faces & face_west ) != 0 ) {
                        emit_wall_side( sink, terrain, lx, ly, face_west, 0.0f, ts.height,
                                         make_ref( lx, ly, -1, 0, 0, face_shade[3] ) );
                    }
                    if( ( faces & face_east ) != 0 ) {
                        emit_wall_side( sink, terrain, lx, ly, face_east, 0.0f, ts.height,
                                         make_ref( lx, ly, 1, 0, 0, face_shade[4] ) );
                    }
                    break;
                case shape_kind::upright:
                    // Trees and shrubs: ground here, the sprite itself is drawn per frame.
                    emit_top_layers( sink, cache.ground( tiles ), lx, ly, floor_height,
                                     make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                    mesh_uprights.push_back( { static_cast<uint8_t>( lx ), static_cast<uint8_t>( ly ),
                                               floor_height, &terrain } );
                    break;
                case shape_kind::pane: {
                    // The frame stands along the wall line; the floor under it is a neighbour's
                    // floor on the open side (inside or out), else the ground.
                    const bool wall_along_y = nb.north.kind == shape_kind::block ||
                                              nb.south.kind == shape_kind::block;
                    const tile_info *under = nullptr;
                    const std::array<point, 2> sides_open = wall_along_y ?
                            std::array<point, 2> { point( -1, 0 ), point( 1, 0 ) } :
                            std::array<point, 2> { point( 0, -1 ), point( 0, 1 ) };
                    for( const point &d : sides_open ) {
                        const tile_info *n = terrain_at( lx + d.x, ly + d.y );
                        if( n && n->sh.kind == shape_kind::floor && !is_tall( n->sprite ) ) {
                            under = n;
                            break;
                        }
                    }
                    emit_top_layers( sink, under ? *under : cache.ground( tiles ), lx, ly,
                                     floor_height, make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
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
            if( fs.kind == shape_kind::floor ) {
                emit_top_layers( sink, furn, lx, ly, fs.height,
                                 make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                continue;
            }
            // A box sized to the sprite's opaque part: lid rows on top, the front rows (tall
            // pieces like lockers) or the lid's front edge (low ones like benches, whose front
            // rows are mostly legs and shadow) on the sides.
            const sprite_ref &spr = furn.main();
            constexpr float low_box = 0.6f;
            const box_layout layout = furniture_box_layout( spr.opaque_x0, spr.opaque_y0, spr.opaque_x1,
                                      spr.opaque_y1, front_fraction( fs.height - ts.height ), fs.height < low_box );
            const std::array<light_ref, 5> refs = {{
                    make_ref( lx, ly, 0, 0, 0, face_shade[0] ), make_ref( lx, ly, 0, -1, 0, face_shade[1] ),
                    make_ref( lx, ly, 0, 1, 0, face_shade[2] ), make_ref( lx, ly, -1, 0, 0, face_shade[3] ),
                    make_ref( lx, ly, 1, 0, 0, face_shade[4] )
                }
            };
            emit_box( sink, spr, lx, ly, ts.height, fs.height, layout, visible_faces( fs, nb ), refs,
                      fs.slab );
        }
    }
    chunk_mesh mesh = sink.finish();
    mesh.uprights = std::move( mesh_uprights );
    return mesh;
}

} // namespace r3d

#endif // defined(TILES)
