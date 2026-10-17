#if defined(TILES)

#include "r3d_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "cata_tiles.h"
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
    t.move_cost = d.movecost;
    return t;
}

sprite_ref resolve_sprite( cata_tiles &tiles, const std::string &id, TILE_CATEGORY category )
{
    const tripoint_bub_ms origin( 0, 0, 0 );
    std::optional<texture_draw_data> data = tiles.get_sprite_draw_data( id, category, origin );
    if( !data ) {
        data = tiles.get_sprite_draw_data( "unknown", TILE_CATEGORY::NONE, origin );
    }
    if( !data || !data->texture ) {
        return {};
    }
    float tex_w = 0.0f;
    float tex_h = 0.0f;
    if( !SDL_GetTextureSize( data->texture, &tex_w, &tex_h ) || tex_w <= 0.0f || tex_h <= 0.0f ) {
        return {};
    }
    // Half a texel in from each edge so nearest sampling never picks up a neighbouring sprite.
    const float half_u = 0.5f / tex_w;
    const float half_v = 0.5f / tex_h;
    sprite_ref ref;
    ref.sheet = data->texture;
    ref.u0 = data->uv0.first + half_u;
    ref.v0 = data->uv0.second + half_v;
    ref.u1 = data->uv1.first - half_u;
    ref.v1 = data->uv1.second - half_v;
    ref.valid = true;
    return ref;
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

struct quad {
    SDL_Texture *sheet = nullptr;
    std::array<vertex, 4> v;
    std::array<light_ref, 4> r;
};

class quad_sink
{
    public:
        // Tile space corners (x east, y south, z up) are flipped into the right-handed world frame.
        void add( const sprite_ref &sprite, const std::array<std::array<float, 3>, 4> &corners,
                  const std::array<std::array<float, 2>, 4> &uvs, const light_ref &ref ) {
            if( !sprite.valid ) {
                return;
            }
            quad q;
            q.sheet = sprite.sheet;
            for( size_t i = 0; i < 4; ++i ) {
                q.v[i] = vertex{ corners[i][0], -corners[i][1], corners[i][2], uvs[i][0], uvs[i][1] };
                q.r[i] = ref;
            }
            quads_.push_back( q );
        }

        chunk_mesh finish() {
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
                    mesh.light.push_back( pack_light( q.r[i].shade, q.r[i].shade, q.r[i].shade ) );
                }
                for( uint32_t idx : { 0u, 1u, 2u, 0u, 2u, 3u } ) {
                    mesh.indices.push_back( base + idx );
                }
                mesh.batches.back().index_count += 6;
            }
            return mesh;
        }

    private:
        std::vector<quad> quads_;
};

using corners_t = std::array<std::array<float, 3>, 4>;
using uvs_t = std::array<std::array<float, 2>, 4>;

// All faces use the full sprite horizontally. Vertically the sprite spans one tile height starting
// at z_base, so a shorter box shows only the lower part of it.
void emit_top( quad_sink &sink, const sprite_ref &s, int x, int y, float z, const light_ref &ref )
{
    const float x0 = static_cast<float>( x );
    const float y0 = static_cast<float>( y );
    const corners_t c = {{ { x0, y0, z }, { x0 + 1, y0, z }, { x0 + 1, y0 + 1, z }, { x0, y0 + 1, z } }};
    const uvs_t uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, s.v1 }, { s.u0, s.v1 } }};
    sink.add( s, c, uv, ref );
}

void emit_side( quad_sink &sink, const sprite_ref &s, int x, int y, face_bit face, float z_base,
                float z_top, const light_ref &ref )
{
    const float x0 = static_cast<float>( x );
    const float y0 = static_cast<float>( y );
    const float dv = s.v1 - s.v0;
    const float v_top = s.v1 - dv * ( z_top - z_base );
    const float v_bot = s.v1;
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

void emit_cross( quad_sink &sink, const sprite_ref &s, int x, int y, float z_base, float height,
                 const light_ref &ref )
{
    const float cx = static_cast<float>( x ) + 0.5f;
    const float cy = static_cast<float>( y ) + 0.5f;
    const float half = height * 0.5f * 0.70710678f; // half width along a diagonal, in x and y
    const float z_top = z_base + height;
    const uvs_t uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, s.v1 }, { s.u0, s.v1 } }};
    const corners_t a = {{ { cx - half, cy - half, z_top }, { cx + half, cy + half, z_top },
            { cx + half, cy + half, z_base }, { cx - half, cy - half, z_base }
        }
    };
    const corners_t b = {{ { cx + half, cy - half, z_top }, { cx - half, cy + half, z_top },
            { cx - half, cy + half, z_base }, { cx + half, cy - half, z_base }
        }
    };
    sink.add( s, a, uv, ref );
    sink.add( s, b, uv, ref );
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

void tile_info_cache::clear()
{
    terrain_.clear();
    furniture_.clear();
    ground_ = {};
    ground_loaded_ = false;
}

const tile_info &tile_info_cache::terrain( ter_id id, cata_tiles &tiles )
{
    auto it = terrain_.find( id.to_i() );
    if( it != terrain_.end() ) {
        return it->second;
    }
    tile_info info;
    const ter_t &t = id.obj();
    info.sh = classify_terrain( traits_of( t ) );
    if( info.sh.kind != shape_kind::empty ) {
        info.sprite = resolve_sprite( tiles, t.id.str(), TILE_CATEGORY::TERRAIN );
    }
    return terrain_.emplace( id.to_i(), info ).first->second;
}

const tile_info &tile_info_cache::furniture( furn_id id, cata_tiles &tiles )
{
    auto it = furniture_.find( id.to_i() );
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
        info.sprite = resolve_sprite( tiles, f.id.str(), TILE_CATEGORY::FURNITURE );
    }
    return furniture_.emplace( id.to_i(), info ).first->second;
}

const sprite_ref &tile_info_cache::ground( cata_tiles &tiles )
{
    if( !ground_loaded_ ) {
        ground_ = resolve_sprite( tiles, "t_grass", TILE_CATEGORY::TERRAIN );
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
    const tripoint_rel_sm neighbours[] = { { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
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

    quad_sink sink;
    for( int ly = 0; ly < side; ++ly ) {
        for( int lx = 0; lx < side; ++lx ) {
            const point_sm_ms p( lx, ly );
            const tile_info &terrain = cache.terrain( self->get_ter( p ), tiles );
            const tile_info &furn = cache.furniture( self->get_furn( p ), tiles );
            neighbourhood nb;
            nb.up = up[static_cast<size_t>( ly * side + lx )];
            nb.north = level_at( lx, ly - 1 );
            nb.south = level_at( lx, ly + 1 );
            nb.west = level_at( lx - 1, ly );
            nb.east = level_at( lx + 1, ly );

            const shape &ts = terrain.sh;
            const uint8_t faces = visible_faces( ts, nb );
            switch( ts.kind ) {
                case shape_kind::floor:
                    if( ( faces & face_top ) != 0 ) {
                        emit_top( sink, terrain.sprite, lx, ly, ts.height,
                                  make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                    }
                    break;
                case shape_kind::block:
                    if( ( faces & face_top ) != 0 ) {
                        emit_top( sink, terrain.sprite, lx, ly, ts.height,
                                  make_ref( lx, ly, 0, 0, 1, face_shade[0] ) );
                    }
                    if( ( faces & face_north ) != 0 ) {
                        emit_side( sink, terrain.sprite, lx, ly, face_north, 0.0f, ts.height,
                                   make_ref( lx, ly, 0, -1, 0, face_shade[1] ) );
                    }
                    if( ( faces & face_south ) != 0 ) {
                        emit_side( sink, terrain.sprite, lx, ly, face_south, 0.0f, ts.height,
                                   make_ref( lx, ly, 0, 1, 0, face_shade[2] ) );
                    }
                    if( ( faces & face_west ) != 0 ) {
                        emit_side( sink, terrain.sprite, lx, ly, face_west, 0.0f, ts.height,
                                   make_ref( lx, ly, -1, 0, 0, face_shade[3] ) );
                    }
                    if( ( faces & face_east ) != 0 ) {
                        emit_side( sink, terrain.sprite, lx, ly, face_east, 0.0f, ts.height,
                                   make_ref( lx, ly, 1, 0, 0, face_shade[4] ) );
                    }
                    break;
                case shape_kind::cross:
                    emit_top( sink, cache.ground( tiles ), lx, ly, floor_height,
                              make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                    emit_cross( sink, terrain.sprite, lx, ly, floor_height, ts.height,
                                make_ref( lx, ly, 0, 0, 0, 230 ) );
                    break;
                case shape_kind::empty:
                case shape_kind::prism:
                    break;
            }

            // Furniture stands on the floor; it is skipped inside walls and under trees.
            const shape &fs = furn.sh;
            if( ts.kind != shape_kind::floor || fs.kind == shape_kind::empty ) {
                continue;
            }
            if( fs.kind == shape_kind::floor ) {
                emit_top( sink, furn.sprite, lx, ly, fs.height,
                          make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
                continue;
            }
            const uint8_t furn_faces = visible_faces( fs, nb );
            if( ( furn_faces & face_top ) != 0 ) {
                emit_top( sink, furn.sprite, lx, ly, fs.height,
                          make_ref( lx, ly, 0, 0, 0, face_shade[0] ) );
            }
            const struct {
                face_bit face;
                int dx;
                int dy;
                uint8_t shade;
            } sides[] = {
                { face_north, 0, -1, face_shade[1] }, { face_south, 0, 1, face_shade[2] },
                { face_west, -1, 0, face_shade[3] }, { face_east, 1, 0, face_shade[4] },
            };
            for( const auto &s : sides ) {
                if( ( furn_faces & s.face ) != 0 ) {
                    emit_side( sink, furn.sprite, lx, ly, s.face, floor_height, fs.height,
                               make_ref( lx, ly, s.dx, s.dy, 0, s.shade ) );
                }
            }
        }
    }
    return sink.finish();
}

} // namespace r3d

#endif // defined(TILES)
