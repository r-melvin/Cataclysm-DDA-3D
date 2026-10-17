#if defined(TILES)

#include "r3d_entities.h"

#include <algorithm>
#include <array>
#include <string>

#include "avatar.h"
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
constexpr float billboard_height = 1.0f;
constexpr float billboard_width = 1.0f;
constexpr float item_size = 0.6f;

// Camera-facing upright quad standing on the tile centre. Corners are in scene-frame tile space.
void emit_billboard( quad_sink &sink, const tile_info &sprite, const vec3 &right_world,
                     float cx, float cy, float z_base, uint32_t color )
{
    // The camera's right vector is in the right-handed world frame; tile space has y flipped.
    const float rx = right_world.x * billboard_width * 0.5f;
    const float ry = -right_world.y * billboard_width * 0.5f;
    const float z_top = z_base + billboard_height;
    const corners_t c = {{ { cx - rx, cy - ry, z_top }, { cx + rx, cy + ry, z_top },
            { cx + rx, cy + ry, z_base }, { cx - rx, cy - ry, z_base }
        }
    };
    auto layer = [&]( const sprite_ref & s, float lift ) {
        if( !s.valid ) {
            return;
        }
        const uvs_t uv = {{ { s.u0, s.v0 }, { s.u1, s.v0 }, { s.u1, s.v1 }, { s.u0, s.v1 } }};
        // Nudge the foreground towards the viewer (opposite the right-vector's normal) via z only.
        corners_t lifted = c;
        for( auto &corner : lifted ) {
            corner[2] += lift;
        }
        sink.add_colored( s, lifted, uv, light_ref{}, color );
    };
    layer( sprite.backdrop, 0.0f );
    layer( sprite.sprite, sprite.backdrop.valid ? 0.004f : 0.0f );
}

// Item sprite lying flat just above the floor.
void emit_flat( quad_sink &sink, const tile_info &sprite, float cx, float cy, float z, uint32_t color )
{
    const float h = item_size * 0.5f;
    const corners_t c = {{ { cx - h, cy - h, z }, { cx + h, cy - h, z }, { cx + h, cy + h, z },
            { cx - h, cy + h, z }
        }
    };
    for( const sprite_ref *s : { &sprite.backdrop, &sprite.sprite } ) {
        if( !s->valid ) {
            continue;
        }
        const uvs_t uv = {{ { s->u0, s->v0 }, { s->u1, s->v0 }, { s->u1, s->v1 }, { s->u0, s->v1 } }};
        sink.add_colored( *s, c, uv, light_ref{}, color );
    }
}

} // namespace

chunk_mesh build_entity_mesh( const entity_view &view )
{
    map &here = get_map();
    const avatar &you = get_avatar();
    quad_sink sink;
    const vec3 right = view.cam.ground_right();
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

    // Items first: top item of every visible tile near the view, drawn flat on the floor.
    for( int z = view.z_lo; z <= view.z_hi; ++z ) {
        for( int dy = -item_radius; dy <= item_radius; ++dy ) {
            for( int dx = -item_radius; dx <= item_radius; ++dx ) {
                const tripoint_bub_ms p( view.center.x() + dx, view.center.y() + dy, z );
                if( !here.inbounds( p ) ) {
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
                emit_flat( sink, sprite, frame_x( p ), frame_y( p ), frame_z( p ) + floor_height + 0.02f,
                           light );
            }
        }
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
        std::string id;
        TILE_CATEGORY category = TILE_CATEGORY::MONSTER;
        if( const monster *m = critter.as_monster() ) {
            id = m->type->id.str();
        } else if( const Character *ch = critter.as_character() ) {
            category = TILE_CATEGORY::NONE;
            id = critter.is_avatar() ? ( ch->male ? "player_male" : "player_female" ) :
                 ( ch->male ? "npc_male" : "npc_female" );
        } else {
            continue;
        }
        const tile_info &sprite = view.cache.named( category, id, view.tiles );
        const uint32_t color = critter.is_avatar() ? white : view.light( p );
        emit_billboard( sink, sprite, right, frame_x( p ), frame_y( p ), frame_z( p ) + floor_height,
                        color == pack_light( 0, 0, 0 ) ? white : color );
    }
    return sink.finish();
}

} // namespace r3d

#endif // defined(TILES)
