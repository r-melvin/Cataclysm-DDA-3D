#if defined(TILES)

#include "r3d_entities.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "debug.h"
#include "cata_tiles.h"
#include "character.h"
#include "creature.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "r3d_shapes.h"

namespace r3d
{

namespace
{

constexpr int creature_radius = 45; // tiles around the view that are searched for creatures
constexpr int item_radius = 20;     // and for items (every tile costs a map lookup)
constexpr float marker_box_height = 1.0f;
constexpr float item_size = 0.6f;

// Light colour with a zero alpha: no depth pull (see r3d_block.vert), for everything in the
// entity mesh that is not the top of a billboard.
uint32_t no_pull( uint32_t color )
{
    return color & 0x00ffffffu;
}

// Camera-facing upright quads standing on the tile centre, sized like the first layer's sprite
// is in the flat view (big trees are several tiles tall). Later layers (clothing over a body) sit
// slightly closer to the camera. Corners are in scene-frame tile space.
void emit_billboard( quad_sink &sink, const std::vector<const tile_info *> &layers,
                     const vec3 &right_world, const vec3 &up_world, const vec3 &toward_world, float z_scale,
                     float cx, float cy, float z_base, uint32_t color, bool mirrored = false,
                     float scale = 1.0f )
{
    if( layers.empty() || !layers.front() ) {
        return;
    }
    const sprite_ref &main = layers.front()->main();
    const float height = std::clamp( main.size_y, 0.25f, 6.0f ) * scale;
    // The camera vectors are in the right-handed world frame; tile space has y flipped.
    const float tx = toward_world.x;
    const float ty = -toward_world.y;
    // The camera vectors are in stretched space, where sprites keep their flat-view proportions;
    // their vertical parts shrink back into world space, where a z-level is 1 high.
    const float tz = toward_world.z / z_scale;
    // Facing the camera squarely leans the top away from it, into whatever stands behind the
    // tile (a wall). Sliding the quad towards the camera along the line of sight keeps it where
    // it is on screen but brings the top back inside the tile's own footprint.
    const float lean = height * std::sqrt( up_world.x * up_world.x + up_world.y * up_world.y );
    const float toward_ground = std::sqrt( toward_world.x * toward_world.x +
                                           toward_world.y * toward_world.y );
    constexpr float half_tile = 0.45f;
    const float pull = std::clamp( ( lean - half_tile ) / std::max( toward_ground, 0.05f ), 0.0f,
                                   max_billboard_pull );
    // The pull only moves depth (see r3d_block.vert): the top's light alpha carries it, the base
    // keeps 0 so feet stay where they stand.
    const uint32_t top_alpha = static_cast<uint32_t>( pull / max_billboard_pull * 255.0f + 0.5f ) << 24;
    const std::array<uint32_t, 4> colors = {{
            ( color & 0x00ffffffu ) | top_alpha, ( color & 0x00ffffffu ) | top_alpha,
            color & 0x00ffffffu, color & 0x00ffffffu
        }
    };
    // The quad faces the camera squarely (leaning back with its up vector), so sprites keep the
    // full size and detail they have in the flat view instead of being squashed by the pitch.
    // Each layer keeps its own sprite's size, standing on the same feet: a 48-row body under
    // 36-row clothing, as the flat view draws them.
    int depth = 0;
    auto layer = [&]( const sprite_ref & s ) {
        if( !s.valid ) {
            return;
        }
        const float lw = std::clamp( s.size_x, 0.25f, 4.0f ) * scale;
        const float lh = std::clamp( s.size_y, 0.25f, 6.0f ) * scale;
        const float rx = right_world.x * lw * 0.5f;
        const float ry = -right_world.y * lw * 0.5f;
        const float ux = up_world.x * lh;
        const float uy = -up_world.y * lh;
        const float uz = up_world.z * lh / z_scale;
        // The flat view draws a sprite offset_y pixels below its tile's top edge, so its bottom
        // row lands (offset_y + height - tile) below the tile's bottom: the same shift here,
        // along the screen's up, keeps a 48-row body and 36-row clothes on the same feet.
        const float drop = ( s.offset_y + s.size_y - 1.0f ) * scale;
        const float side = s.offset_x * scale;
        const float lift = 0.01f * static_cast<float>( depth++ );
        const float ox = cx + tx * lift - up_world.x * drop + right_world.x * side;
        const float oy = cy + ty * lift + up_world.y * drop - right_world.y * side;
        const float oz = z_base + tz * lift - up_world.z * drop / z_scale;
        const float ul = mirrored ? s.u1 : s.u0;
        const float ur = mirrored ? s.u0 : s.u1;
        const uvs_t uv = {{ { ul, s.v0 }, { ur, s.v0 }, { ur, s.v1 }, { ul, s.v1 } }};
        const corners_t c = {{ { ox - rx + ux, oy - ry + uy, oz + uz }, { ox + rx + ux, oy + ry + uy, oz + uz },
                { ox + rx, oy + ry, oz }, { ox - rx, oy - ry, oz }
            }
        };
        sink.add_colored( s, c, uv, light_ref{}, colors );
    };
    for( const tile_info *t : layers ) {
        if( t ) {
            layer( t->backdrop );
            layer( t->sprite );
        }
    }
}

// Sprite lying flat at height z, size tiles wide.
void emit_flat( quad_sink &sink, const tile_info &sprite, float cx, float cy, float z, uint32_t color,
                float size = item_size )
{
    const float h = size * 0.5f;
    const corners_t c = {{ { cx - h, cy - h, z }, { cx + h, cy - h, z }, { cx + h, cy + h, z },
            { cx - h, cy + h, z }
        }
    };
    for( const sprite_ref *s : { &sprite.backdrop, &sprite.sprite } ) {
        if( !s->valid ) {
            continue;
        }
        const uvs_t uv = {{ { s->u0, s->v0 }, { s->u1, s->v0 }, { s->u1, s->v1 }, { s->u0, s->v1 } }};
        sink.add_colored( *s, c, uv, light_ref{}, no_pull( color ) );
    }
}

// Box standing on a tile with the sprite on its four sides, so a cursor reads as a 3D outline.
void emit_marker_box( quad_sink &sink, const sprite_ref &s, float x0, float y0, float z0, float z1,
                      uint32_t color )
{
    const float x1 = x0 + 1.0f;
    const float y1 = y0 + 1.0f;
    const uvs_t uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, s.v1 }, { s.u0, s.v1 } }};
    const std::array<std::array<float, 4>, 4> sides = {{
            { x0, y1, x1, y1 }, { x1, y0, x0, y0 }, { x1, y1, x1, y0 }, { x0, y0, x0, y1 }
        }
    };
    for( const std::array<float, 4> &e : sides ) {
        const corners_t c = {{ { e[0], e[1], z1 }, { e[2], e[3], z1 }, { e[2], e[3], z0 },
                { e[0], e[1], z0 }
            }
        };
        sink.add_colored( s, c, uv, light_ref{}, no_pull( color ) );
    }
}

} // namespace

chunk_mesh build_entity_mesh( const entity_view &view )
{
    map &here = get_map();
    const avatar &you = get_avatar();
    quad_sink sink;
    const vec3 right = view.cam.ground_right();
    // Camera vectors in the stretched space the picture is in (see camera::z_scale).
    const vec3 toward = normalize( view.cam.eye() - view.cam.stretched_target() );
    const vec3 up = cross( right, vec3{ 0.0f, 0.0f, 0.0f } - toward );
    const uint32_t white = pack_light( 255, 255, 255 );

    auto in_range = [&]( const tripoint_bub_ms & p, int radius ) {
        return p.z() >= view.z_lo && p.z() <= view.z_hi &&
               std::abs( p.x() - view.center.x() ) <= radius &&
               std::abs( p.y() - view.center.y() ) <= radius;
    };
    auto frame_x = [&]( const tripoint_bub_ms & p ) {
        return static_cast<float>( p.x() - view.center.x() ) + 0.5f;
    };
    auto frame_y = [&]( const tripoint_bub_ms & p ) {
        return static_cast<float>( p.y() - view.center.y() ) + 0.5f;
    };
    auto frame_z = [&]( const tripoint_bub_ms & p ) {
        return static_cast<float>( p.z() - view.center.z() );
    };

    // Items first: top item of every visible tile near the view, standing on its surface.
    for( int z = view.z_lo; z <= view.z_hi; ++z ) {
        for( int dy = -item_radius; dy <= item_radius; ++dy ) {
            for( int dx = -item_radius; dx <= item_radius; ++dx ) {
                const tripoint_bub_ms p( view.center.x() + dx, view.center.y() + dy, z );
                if( !here.inbounds( p ) ) {
                    continue;
                }
                // Nothing on tiles the player cannot see into, and nothing inside closed
                // furniture (lockers, cupboards): the flat view shows those once the player
                // stands next to them, but here the furniture is drawn closed, and its
                // contents would sit on its top (or float there when the cutaway removes it).
                if( !here.sees_some_items( p, you ) ||
                    here.has_flag( ter_furn_flag::TFLAG_CONTAINER, p ) ) {
                    continue;
                }
                map_stack stack = here.i_at( p );
                if( stack.empty() ) {
                    continue;
                }
                const uint32_t light = view.light( p );
                if( light == pack_light( 0, 0, 0 ) ) {
                    continue; // not visible
                }
                const std::string id = stack.begin()->typeId().str();
                const tile_info &sprite = view.cache.named( TILE_CATEGORY::ITEM, id, view.tiles );
                // Keep the item's illustrated profile visible at every camera angle, like
                // creature sprites, rather than projecting it onto the ground.
                emit_billboard( sink, { &sprite }, right, up, toward, view.cam.z_scale,
                                frame_x( p ), frame_y( p ), frame_z( p ) + view.surface( p ) + 0.02f,
                                light, false, item_size );
            }
        }
    }

    // Trees and tall furniture of the visible chunks; unseen ones are drawn like their tile.
    for( const entity_view::standing &u : view.uprights ) {
        if( !u.info ) {
            continue;
        }
        emit_billboard( sink, { u.info }, right, up, toward, view.cam.z_scale, frame_x( u.pos ), frame_y( u.pos ),
                        frame_z( u.pos ) + u.z_base, view.light( u.pos ) );
    }

    // Creatures: the player, NPCs and monsters that the player can see.
    for( Creature &critter : g->all_creatures() ) {
        const tripoint_bub_ms p = critter.pos_bub();
        if( !in_range( p, creature_radius ) ) {
            continue;
        }
        if( !critter.is_avatar() && !you.sees( here, critter ) ) {
            continue;
        }
        // Characters are their body plus worn and wielded overlays, like the flat view.
        std::vector<const tile_info *> layers;
        bool mirrored = false;
        if( const monster *m = critter.as_monster() ) {
            layers.push_back( &view.cache.named( TILE_CATEGORY::MONSTER, m->type->id.str(), view.tiles ) );
        } else if( const Character *ch = critter.as_character() ) {
            const cata_tiles::character_layers looks = view.tiles.get_character_layers( *ch );
            mirrored = looks.mirrored;
            for( const std::pair<std::string, TILE_CATEGORY> &id : looks.ids ) {
                layers.push_back( &view.cache.named( id.second, id.first, view.tiles ) );
            }
        } else {
            continue;
        }
        const uint32_t color = critter.is_avatar() ? white : view.light( p );
        // Debug aid: CATA_R3D_DEBUG_LAYERS=1 logs the sprite layers of the player once per run.
        static const bool debug_layers = std::getenv( "CATA_R3D_DEBUG_LAYERS" ) != nullptr;
        static bool logged = false;
        if( debug_layers && critter.is_avatar() && !logged ) {
            logged = true;
            const cata_tiles::character_layers looks = view.tiles.get_character_layers( *critter.as_character() );
            for( size_t i = 0; i < looks.ids.size() && i < layers.size(); ++i ) {
                const sprite_ref &m = layers[i]->main();
                DebugLog( D_INFO, DC_ALL ) << "r3d layer " << i << " " << looks.ids[i].first << " valid=" << m.valid
                                           << " size=" << m.size_x << "x" << m.size_y << " uv=" << m.u0 << "," << m.v0
                                           << " backdrop=" << layers[i]->backdrop.valid;
            }
        }
        emit_billboard( sink, layers, right, up, toward, view.cam.z_scale, frame_x( p ), frame_y( p ),
                        frame_z( p ) + view.surface( p ), color == pack_light( 0, 0, 0 ) ? white : color,
                        mirrored );
    }
    // Markers last; they sit just above the surface so the depth test keeps them in front of it.
    for( const entity_view::marker &m : view.markers ) {
        if( !m.sprite || m.pos.z() < view.z_lo || m.pos.z() > view.z_hi ) {
            continue;
        }
        const float x0 = static_cast<float>( m.pos.x() - view.center.x() );
        const float y0 = static_cast<float>( m.pos.y() - view.center.y() );
        const float z = frame_z( m.pos ) + m.top + 0.03f;
        emit_flat( sink, *m.sprite, x0 + 0.5f, y0 + 0.5f, z, white, 1.0f );
        if( m.box ) {
            const sprite_ref &s = m.sprite->sprite.valid ? m.sprite->sprite : m.sprite->backdrop;
            if( s.valid ) {
                emit_marker_box( sink, s, x0, y0, z, z + marker_box_height, white );
            }
        }
    }
    return sink.finish();
}

} // namespace r3d

#endif // defined(TILES)
