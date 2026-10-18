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
#include "r3d_hud.h"
#include "r3d_shapes.h"
#include "enums.h"
#include "units_utility.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vpart_position.h"

namespace r3d
{

namespace
{

constexpr int creature_radius = 45; // tiles around the view that are searched for creatures
constexpr int item_radius = 20;     // and for items (every tile costs a map lookup)
constexpr float marker_box_height = 1.0f;
constexpr float item_size = 0.6f;
constexpr float item_stand_size = 0.45f; // first person: size of an item standing on the floor
constexpr float post_width = 0.3f;       // first person: markers along a line of fire
constexpr float post_height = 0.6f;
constexpr float heard_post_width = 0.35f; // first person: a sound heard there this turn
constexpr float heard_post_height = 0.8f;
// First person: the nearest an upright sprite's quad comes to the eye along the view.
constexpr float min_sprite_depth = 0.35f;
constexpr float vehicle_body_height = 0.25f;

// Light colour with a zero alpha: no depth pull (see r3d_block.vert), for everything in the
// entity mesh that is not the top of a billboard.
uint32_t no_pull( uint32_t color )
{
    return color & 0x00ffffffu;
}

// Geometry and depth shared by the sprite layers of one billboard.
struct billboard_geometry {
    quad_sink &sink;
    const vec3 &right_world;
    const vec3 &up_world;
    float scale;
    float z_scale;
    float tx;
    float ty;
    float tz;
    float cx;
    float cy;
    float z_base;
    bool mirrored;
    int depth;
    const std::array<uint32_t, 4> &colors;

    void emit_layer( const sprite_ref &s ) {
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
    }
};

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
    billboard_geometry geometry{ sink, right_world, up_world, scale, z_scale, tx, ty, tz,
                                 cx, cy, z_base, mirrored, 0, colors };
    for( const tile_info *t : layers ) {
        if( t ) {
            if( !t->sh.ground_backdrop ) {
                geometry.emit_layer( t->backdrop );
            }
            geometry.emit_layer( t->sprite );
        }
    }
}

// Sprite lying flat at height z, size tiles wide.
void emit_flat( quad_sink &sink, const tile_info &sprite, float cx, float cy, float z,
                uint32_t color,
                float size = item_size )
{
    const float h = size * 0.5f;
    const corners_t c = {{ { cx - h, cy - h, z }, { cx + h, cy - h, z }, { cx + h, cy + h, z },
            { cx - h, cy + h, z }
        }
    };
    for( const sprite_ref *s : {
             &sprite.backdrop, &sprite.sprite
         } ) {
        if( !s->valid ) {
            continue;
        }
        const uvs_t uv = {{ { s->u0, s->v0 }, { s->u1, s->v0 }, { s->u1, s->v1 }, { s->u0, s->v1 } }};
        sink.add_colored( *s, c, uv, light_ref{}, no_pull( color ) );
    }
}

// Sprite standing upright on the floor at (cx, cy), turned towards the screen (right is the unit
// vector along the screen's horizontal, world space), size tiles wide and tall in stretched space.
void emit_stand( quad_sink &sink, const tile_info &sprite, const vec3 &right_world, float z_scale,
                 float cx, float cy, float z, uint32_t color, float size = item_stand_size )
{
    const float rx = right_world.x * size * 0.5f;
    const float ry = -right_world.y * size * 0.5f; // tile space has y flipped
    const float top = z + size / z_scale;
    const corners_t c = {{ { cx - rx, cy - ry, top }, { cx + rx, cy + ry, top }, { cx + rx, cy + ry, z },
            { cx - rx, cy - ry, z }
        }
    };
    for( const sprite_ref *s : {
             &sprite.backdrop, &sprite.sprite
         } ) {
        if( !s->valid ) {
            continue;
        }
        const uvs_t uv = {{ { s->u0, s->v0 }, { s->u1, s->v0 }, { s->u1, s->v1 }, { s->u0, s->v1 } }};
        sink.add_colored( *s, c, uv, light_ref{}, no_pull( color ) );
    }
}

// Box standing on a tile with the sprite on its four sides, so a cursor reads as a 3D outline.
// size is the footprint's width; the box is centred on the tile.
void emit_marker_box( quad_sink &sink, const sprite_ref &s, float x0, float y0, float z0, float z1,
                      uint32_t color, float size = 1.0f )
{
    const uint32_t vertex_color = no_pull( color );
    x0 += ( 1.0f - size ) * 0.5f;
    y0 += ( 1.0f - size ) * 0.5f;
    const float x1 = x0 + size;
    const float y1 = y0 + size;
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
        sink.add_colored( s, c, uv, light_ref{}, vertex_color );
    }
}


// Vehicle art is a plan view. Keep it in the map plane, raised on a shallow body,
// rather than turning every part towards the camera independently.
void emit_vehicle_part( quad_sink &sink, const tile_info &info, float x, float y, float z,
                        uint32_t color )
{
    float lift = 0.0f;
    for( const sprite_ref *sprite : {
             &info.backdrop, &info.sprite
         } ) {
        if( !sprite->valid ) {
            continue;
        }
        const sprite_ref &s = *sprite;
        // Vehicle tiles can overhang their cell (doors and windscreens often use
        // 32x48/64 artwork). Preserve the regular renderer's size and offsets.
        const float x0 = x + s.offset_x;
        const float y0 = y + s.offset_y;
        corners_t top = {{ { x0, y0, z + vehicle_body_height + lift },
                { x0 + s.size_x, y0, z + vehicle_body_height + lift },
                { x0 + s.size_x, y0 + s.size_y, z + vehicle_body_height + lift },
                { x0, y0 + s.size_y, z + vehicle_body_height + lift }
            }
        };
        for( auto &corner : top ) {
            for( int turn = 0; turn < ( s.quarter_turns & 3 ); ++turn ) {
                const float dx = corner[0] - x - 0.5f;
                const float dy = corner[1] - y - 0.5f;
                corner[0] = x + 0.5f - dy;
                corner[1] = y + 0.5f + dx;
            }
        }
        const float left = s.flip ? s.u1 : s.u0;
        const float right = s.flip ? s.u0 : s.u1;
        const uvs_t uv = {{ { left, s.v0 }, { right, s.v0 }, { right, s.v1 }, { left, s.v1 } }};
        sink.add_colored( s, top, uv, light_ref{}, no_pull( color ) );
        emit_marker_box( sink, s, x, y, z, z + vehicle_body_height + lift, color,
                         1.0f + 2.0f * lift );
        // The foreground must pass the depth test over the background on every face.
        lift += 0.004f;
    }
}

void add_vehicles( quad_sink &sink, const entity_view &view, map &here, const avatar &you )
{
    for( const wrapped_vehicle &wrapped : here.get_vehicles() ) {
        const vehicle &veh = *wrapped.v;
        const int rotation = angle_to_dir4( veh.face.dir() - 270_degrees );
        for( const tripoint_abs_ms &absolute : veh.get_points() ) {
            const tripoint_bub_ms p = here.get_bub( absolute );
            if( !here.inbounds( p ) || p.z() < view.z_lo || p.z() > view.z_hi ||
                !you.sees( here, p ) ) {
                continue;
            }
            const uint32_t light = view.light( p );
            if( light == pack_light( 0, 0, 0 ) ) {
                continue;
            }
            const optional_vpart_position part = here.veh_at( p );
            if( !part || &part->vehicle() != &veh ) {
                continue;
            }
            const vpart_display display = veh.get_display_of_tile( part->mount_pos() );
            if( display.id.is_null() ) {
                continue;
            }
            const int subtile = display.is_open ? open_ : display.is_broken ? broken : 0;
            const tile_info &sprite = view.cache.named( TILE_CATEGORY::VEHICLE_PART,
                                      "vp_" + display.id.str(), view.tiles, subtile, rotation, display.variant.id );
            const float x = static_cast<float>( p.x() - view.center.x() );
            const float y = static_cast<float>( p.y() - view.center.y() );
            const float z = static_cast<float>( p.z() - view.center.z() ) + view.surface( p ) + 0.02f;
            emit_vehicle_part( sink, sprite, x, y, z, light );
            if( display.has_cargo ) {
                const tile_info &highlight = view.cache.named( TILE_CATEGORY::NONE, "highlight", view.tiles );
                emit_flat( sink, highlight, x + 0.5f, y + 0.5f, z + vehicle_body_height + 0.02f,
                           pack_light( 255, 255, 255 ), 1.0f );
            }
            if( !display.carried_furn.empty() ) {
                const tile_info &furniture = view.cache.named( TILE_CATEGORY::FURNITURE,
                                             display.carried_furn, view.tiles );
                emit_vehicle_part( sink, furniture, x, y, z + 0.01f, light );
            }
        }
    }
}

} // namespace

chunk_mesh build_entity_mesh( const entity_view &view )
{
    map &here = get_map();
    const avatar &you = get_avatar();
    quad_sink sink;
    const bool upright = view.upright_billboards;
    const vec3 right = view.cam.ground_right();
    // Camera vectors in the stretched space the picture is in (see camera::z_scale).
    vec3 toward = normalize( view.cam.eye() - view.cam.stretched_target() );
    vec3 up = cross( right, vec3{ 0.0f, 0.0f, 0.0f } - toward );
    if( upright ) {
        // Cylindrical billboards: straight up, facing the screen, so nothing leans and nothing
        // needs its depth pulled (see r3d_block.vert). The horizontal view direction orders
        // the layers of a sprite.
        const vec3 ahead = view.cam.fp_forward();
        toward = normalize( vec3{ -ahead.x, -ahead.y, 0.0f } );
        up = { 0.0f, 0.0f, 1.0f };
    }
    const uint32_t white = pack_light( 255, 255, 255 );
    // First person: the eye and the view direction on the ground, in the scene's tile space
    // (y south), for sliding sprites away from the eye.
    const vec3 eye_world = view.cam.eye();
    const vec3 eye_tile{ eye_world.x, -eye_world.y, 0.0f };
    const vec3 ahead_world = view.cam.fp_forward();
    const float ahead_len = std::sqrt( ahead_world.x * ahead_world.x + ahead_world.y * ahead_world.y );
    const float ahead_x = ahead_len > 0.0f ? ahead_world.x / ahead_len : 0.0f;
    const float ahead_y = ahead_len > 0.0f ? -ahead_world.y / ahead_len : 1.0f;

    auto in_range = [ &view ]( const tripoint_bub_ms & p, int radius ) {
        return p.z() >= view.z_lo && p.z() <= view.z_hi &&
               std::abs( p.x() - view.center.x() ) <= radius &&
               std::abs( p.y() - view.center.y() ) <= radius;
    };
    auto frame_x = [ &view ]( const tripoint_bub_ms & p ) {
        return static_cast<float>( p.x() - view.center.x() ) + 0.5f;
    };
    auto frame_y = [ &view ]( const tripoint_bub_ms & p ) {
        return static_cast<float>( p.y() - view.center.y() ) + 0.5f;
    };
    auto frame_z = [ &view ]( const tripoint_bub_ms & p ) {
        return static_cast<float>( p.z() - view.center.z() );
    };

    add_vehicles( sink, view, here, you );

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
                if( upright ) {
                    emit_stand( sink, sprite, right, view.cam.z_scale, frame_x( p ), frame_y( p ),
                                frame_z( p ) + view.surface( p ) + 0.02f, light );
                    continue;
                }
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
        emit_billboard( sink, { u.info }, right, up, toward, view.cam.z_scale, frame_x( u.pos ),
                        frame_y( u.pos ),
                        frame_z( u.pos ) + u.z_base, view.light( u.pos ) );
    }

    // Creatures: the player, NPCs and monsters that the player can see.
    for( Creature &critter : g->all_creatures() ) {
        const tripoint_bub_ms p = critter.pos_bub();
        if( !in_range( p, creature_radius ) ) {
            continue;
        }
        if( critter.is_avatar() ? upright : !you.sees( here, critter ) ) {
            continue; // you do not see yourself from inside your head
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
            const cata_tiles::character_layers looks = view.tiles.get_character_layers(
                        *critter.as_character() );
            for( size_t i = 0; i < looks.ids.size() && i < layers.size(); ++i ) {
                const sprite_ref &m = layers[i]->main();
                DebugLog( D_INFO, DC_ALL ) << "r3d layer " << i << " " << looks.ids[i].first << " valid=" << m.valid
                                           << " size=" << m.size_x << "x" << m.size_y << " uv=" << m.u0 << "," << m.v0
                                           << " backdrop=" << layers[i]->backdrop.valid;
            }
        }
        float cx = frame_x( p );
        float cy = frame_y( p );
        if( upright ) {
            // A creature next to the eye (while a step is eased in, the eye passes close to
            // tiles) is slid back along the view direction rather than cut by the near plane or
            // filling the screen. Its depth against walls behind it is not an issue: those are
            // at least half a tile further on.
            const float slide = slide_from_eye( cx, cy, eye_tile.x, eye_tile.y, ahead_x, ahead_y,
                                                min_sprite_depth );
            cx += ahead_x * slide;
            cy += ahead_y * slide;
        }
        emit_billboard( sink, layers, right, up, toward, view.cam.z_scale, cx, cy,
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
        if( m.heard ) {
            const sprite_ref &s = m.sprite->main();
            if( upright && s.valid ) {
                emit_marker_box( sink, s, x0, y0, z, z + heard_post_height, white, heard_post_width );
            }
            continue;
        }
        emit_flat( sink, *m.sprite, x0 + 0.5f, y0 + 0.5f, z, white, 1.0f );
        if( upright && !m.box && !m.large ) {
            // A point along a line of fire: a post that can be seen from eye level.
            const sprite_ref &s = m.sprite->main();
            if( s.valid ) {
                emit_marker_box( sink, s, x0, y0, z, z + post_height, white, post_width );
            }
        } else if( upright && !m.box ) {
            const sprite_ref &s = m.sprite->main();
            if( s.valid ) {
                emit_marker_box( sink, s, x0, y0, z, z + marker_box_height, white );
            }
        }
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
