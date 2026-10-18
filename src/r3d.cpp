#if defined(TILES)

#include "r3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_path.h"
#include "cata_tiles.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "game_constants.h"
#include "json_loader.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "options.h"
#include "path_info.h"
#include "r3d_camera.h"
#include "r3d_dda.h"
#include "r3d_entities.h"
#include "r3d_gpu.h"
#include "r3d_hud.h"
#include "r3d_mesh.h"
#include "sdl_wrappers.h"
#include "sdltiles.h"
#include "sounds.h"
#include "string_formatter.h"
#include "submap.h"
#include "translations.h"
#include "ui_manager.h"
#include "uistate.h"

namespace r3d
{

namespace
{

constexpr int max_chunk_builds_per_frame = 8;
constexpr uint64_t chunk_idle_frames = 600;
// Tallest geometry in a chunk: upright sprites (big trees) can stand several tiles tall.
constexpr float chunk_max_height = 4.0f;

struct chunk_key {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator==( const chunk_key &o ) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct chunk_key_hash {
    size_t operator()( const chunk_key &k ) const {
        return ( static_cast<size_t>( k.x ) * 73856093u ) ^ ( static_cast<size_t>( k.y ) * 19349663u ) ^
               ( static_cast<size_t>( k.z ) * 83492791u );
    }
};

struct chunk_gpu {
    bool built = false;
    uint64_t stamp = 0;
    uint64_t last_used = 0;
    SDL_GPUBuffer *positions = nullptr;
    SDL_GPUBuffer *lights = nullptr;
    SDL_GPUBuffer *indices = nullptr;
    std::vector<mesh_batch> batches;
    std::vector<upright_sprite> uprights; // drawn as billboards every frame
    std::vector<light_ref> refs; // where each vertex samples its light
    std::vector<uint32_t> light; // current RGBA8 light per vertex
    uint64_t light_key = 0;      // light_key() the stream was last computed for
};

// What the last drawn frame showed, so mouse picks match the picture on screen.
struct shown_view {
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    camera cam;
    tripoint_bub_ms center;
    int z_lo = 0;
    int z_hi = 0;
};

struct state {
    gpu device;
    tile_info_cache cache;
    std::unordered_map<chunk_key, chunk_gpu, chunk_key_hash> chunks;
    camera cam;
    unsigned epoch = 0;
    bool epoch_valid = false;
    bool init_failed = false;
    unsigned failed_epoch = 0;
    uint64_t frame = 0;
    bool drawn = false;
    // The tileset load the cached sprites and meshes were built from.
    uint64_t tileset_loads = 0;
    bool env_camera_applied = false;
    bool screenshot_taken = false;
    // Per-frame buffers for creatures and items, grown as needed and re-uploaded every frame.
    SDL_GPUBuffer *dyn_positions = nullptr;
    SDL_GPUBuffer *dyn_lights = nullptr;
    SDL_GPUBuffer *dyn_indices = nullptr;
    uint32_t dyn_position_bytes = 0;
    uint32_t dyn_light_bytes = 0;
    uint32_t dyn_index_bytes = 0;
    shown_view shown;
    bool map_drawn = false; // the game's map view has been on screen (a game is running)
    vec3 pan; // free camera: offset of the camera target from the viewed tile, world space
    bool walls_on = false; // whether the cache was given the wall side rules and atlas
    bool thin_walls = true;
    bool plaster_atlas = false; // whether painted walls take the atlas plaster (see the option)
    bool chunks_first_person = false; // the mode the chunk meshes were built for
    // First-person easing: the player and the heading jump, the picture catches up. The offsets
    // are what is left to show, measured from the logical camera back to where it was.
    bool fp_prev_valid = false;
    tripoint_abs_ms fp_prev_pos;
    int fp_prev_heading = 0;
    float fp_ease_dx = 0.0f; // tiles east and south at the start of the ease
    float fp_ease_dy = 0.0f;
    float fp_ease_dz = 0.0f; // levels up at the start of the ease (stairs, climbing on things)
    float fp_prev_surface = 0.0f;
    float fp_ease_yaw = 0.0f; // radians at the start of the ease
    uint32_t fp_ease_start = 0;
    uint32_t fp_ease_ms = 0;
    bool fp_easing = false; // the last frame was drawn mid-ease, so another is needed
    int fp_heading_saved = 0; // the heading last written to the save's UI state
    std::vector<hud_text> hud; // labels of the first-person overlay, see hud_texts()
    // Sounds heard this turn whose post cannot be seen (out of the picture or behind something):
    // the compass strip shows a tick for each.
    std::vector<tripoint_bub_ms> heard_ticks;
    int hud_cell_w = 8;        // the size of a character of the font they are drawn in
    int hud_cell_h = 16;
};

state &st()
{
    // Intentionally leaked: the GPU objects must not be destroyed after SDL has shut down.
    static state *s = new state();
    return *s;
}

void release_dynamic( state &s )
{
    s.device.release_buffer( s.dyn_positions );
    s.device.release_buffer( s.dyn_lights );
    s.device.release_buffer( s.dyn_indices );
    s.dyn_positions = nullptr;
    s.dyn_lights = nullptr;
    s.dyn_indices = nullptr;
    s.dyn_position_bytes = 0;
    s.dyn_light_bytes = 0;
    s.dyn_index_bytes = 0;
}

// Makes sure a dynamic buffer holds at least bytes, then queues the data for upload.
bool upload_dynamic( state &s, SDL_GPUBuffer *&buffer, uint32_t &capacity,
                     SDL_GPUBufferUsageFlags usage,
                     const void *data, uint32_t bytes )
{
    if( !buffer || capacity < bytes ) {
        s.device.release_buffer( buffer );
        capacity = std::max( bytes, capacity * 2 );
        buffer = s.device.create_buffer( usage, capacity );
        if( !buffer ) {
            capacity = 0;
            return false;
        }
    }
    return s.device.upload( buffer, data, bytes, true );
}

void release_chunk( state &s, chunk_gpu &c )
{
    s.device.release_buffer( c.positions );
    s.device.release_buffer( c.lights );
    s.device.release_buffer( c.indices );
    c.positions = nullptr;
    c.lights = nullptr;
    c.indices = nullptr;
    c.batches.clear();
    c.uprights.clear();
    c.refs.clear();
    c.light.clear();
    c.light_key = 0;
    c.built = false;
}

void release_all_chunks( state &s )
{
    for( auto &entry : s.chunks ) {
        release_chunk( s, entry.second );
    }
    s.chunks.clear();
}

bool rebuild_chunk( state &s, chunk_gpu &c, const tripoint_abs_sm &pos, uint64_t stamp,
                    const submap_lookup &lookup )
{
    mesh_options options;
    options.first_person = s.chunks_first_person;
    options.ceilings = s.chunks_first_person;
    chunk_mesh mesh = build_chunk_mesh( pos, lookup, s.cache, *tilecontext, options );
    release_chunk( s, c );
    c.stamp = stamp;
    c.built = true;
    c.uprights = std::move( mesh.uprights );
    if( mesh.indices.empty() ) {
        return true;
    }
    const uint32_t vertex_bytes = static_cast<uint32_t>( mesh.vertices.size() * sizeof( vertex ) );
    const uint32_t light_bytes = static_cast<uint32_t>( mesh.light.size() * sizeof( uint32_t ) );
    const uint32_t index_bytes = static_cast<uint32_t>( mesh.indices.size() * sizeof( uint32_t ) );
    c.positions = s.device.create_buffer( SDL_GPU_BUFFERUSAGE_VERTEX, vertex_bytes );
    c.lights = s.device.create_buffer( SDL_GPU_BUFFERUSAGE_VERTEX, light_bytes );
    c.indices = s.device.create_buffer( SDL_GPU_BUFFERUSAGE_INDEX, index_bytes );
    if( !c.positions || !c.lights || !c.indices ||
        !s.device.upload( c.positions, mesh.vertices.data(), vertex_bytes ) ||
        !s.device.upload( c.indices, mesh.indices.data(), index_bytes ) ) {
        release_chunk( s, c );
        return false;
    }
    c.batches = std::move( mesh.batches );
    c.refs = std::move( mesh.light_refs );
    c.light = std::move( mesh.light );
    return true;
}

// Loads data/raw/r3d/wall_sides.{json,png} (see make_wall_textures.py) onto the renderer.
// Nothing (the tileset sprites stay on wall sides) if either is missing or broken.
std::shared_ptr<const wall_sides> load_wall_sides( gpu &device )
{
    try {
        const cata_path dir = PATH_INFO::datadir_path() / "raw" / "r3d";
        std::optional<JsonValue> json = json_loader::from_path_opt( dir / "wall_sides.json" );
        if( !json ) {
            return nullptr;
        }
        JsonObject jo = *json;
        jo.allow_omitted_members();
        std::shared_ptr<wall_sides> sides = std::make_shared<wall_sides>();
        sides->cell = jo.get_int( "cell" );
        for( const std::string name : jo.get_array( "textures" ) ) {
            sides->names.push_back( name );
        }
        for( const JsonMember m : jo.get_object( "by_id" ) ) {
            sides->rules.by_id[m.name()] = m.get_string();
        }
        for( const JsonMember m : jo.get_object( "by_prefix" ) ) {
            sides->rules.by_prefix[m.name()] = m.get_string();
        }
        sides->rules.fallback = jo.get_string( "fallback", "" );
        if( jo.has_member( "colors" ) ) {
            const JsonObject colors = jo.get_object( "colors" );
            for( const JsonMember m : colors ) {
                const std::vector<int> rgb = colors.get_int_array( m.name() );
                if( rgb.size() == 3 ) {
                    sides->colors[m.name()] = static_cast<uint32_t>( rgb[0] ) << 16 |
                                              static_cast<uint32_t>( rgb[1] ) << 8 | static_cast<uint32_t>( rgb[2] );
                }
            }
        }

        const SDL_Surface_Ptr surface = load_image( ( dir / "wall_sides.png" ).generic_u8string().c_str() );
        // Uploaded by the 3D renderer itself: a texture made through the SDL_Renderer would
        // only receive its pixels at the renderer's next present.
        sides->atlas = device.create_sheet( surface.get() );
        if( !sides->atlas ) {
            return nullptr;
        }
        sides->width = surface->w;
        sides->height = surface->h;
        return sides;
    } catch( const std::exception &e ) {
        DebugLog( D_WARNING, DC_ALL ) << "r3d: wall side textures not loaded: " << e.what();
        return nullptr;
    }
}

// Height of the solid part of a tile as drawn: 0 for open air, the floor slab, furniture or a
// wall up to 1. Used for picking and for placing markers on top of things.
float column_height( state &s, const map &here, const tripoint_bub_ms &p )
{
    if( !here.inbounds( p ) ) {
        return 0.0f;
    }
    const shape t = s.cache.terrain( here.ter( p ), *tilecontext ).sh;
    float h = t.height;
    if( t.kind == shape_kind::floor ) {
        h = std::max( h, s.cache.furniture( here.furn( p ), *tilecontext ).sh.height );
    }
    return std::min( h, 1.0f );
}

// Tint of tiles that are remembered but not currently seen: a light sepia, close to how the
// flat view draws its memory (color_pixel_sepia_light), so known rooms stay readable.
uint32_t remembered_light()
{
    return pack_light( 205, 188, 160 );
}

// Marker light of a never-seen tile under a roof (see light_sampler and update_chunk_light).
uint32_t unseen_indoor_light()
{
    return pack_light( 1, 1, 1 );
}

// Where things on a tile stand: the floor, or the top of furniture that can be climbed onto
// (benches, counters, tables are boxes; anything that is not a full tile can be stood on).
float surface_height( state &s, const map &here, const tripoint_bub_ms &p )
{
    if( !here.inbounds( p ) ) {
        return floor_height;
    }
    const shape t = s.cache.terrain( here.ter( p ), *tilecontext ).sh;
    if( t.kind != shape_kind::floor ) {
        return floor_height;
    }
    const shape f = s.cache.furniture( here.furn( p ), *tilecontext ).sh;
    return f.kind == shape_kind::prism || f.kind == shape_kind::block ?
           std::max( t.height, f.height ) : t.height;
}

// Turns the game's own visibility knowledge into a light colour per tile: lit tiles are shown
// normally, unseen but remembered ones dim and bluish, everything else black.
struct light_sampler {
    const map &here;
    const visibility_variables &vvc;
    const avatar &you;
    // Lowest level whose visibility cache the game keeps up to date for this view (see
    // map::update_visibility_cache); anything below can only show what is remembered.
    int vision_floor = -OVERMAP_DEPTH;
    // Levels above this have no visibility cache of their own (the game only computes up to
    // the viewed level); they take the light of the ground below them.
    int vision_top = OVERMAP_HEIGHT;

    // Whether any of the eight tiles around p is seen or remembered.
    bool borders_known( const tripoint_bub_ms &p ) const {
        for( int dy = -1; dy <= 1; ++dy ) {
            for( int dx = -1; dx <= 1; ++dx ) {
                if( dx == 0 && dy == 0 ) {
                    continue;
                }
                const tripoint_bub_ms n = p + tripoint_rel_ms( dx, dy, 0 );
                if( !here.inbounds( n ) ) {
                    continue;
                }
                const lit_level ll = here.access_cache( n.z() ).visibility_cache[n.x()][n.y()];
                const visibility_type vis = here.get_visibility( ll, vvc );
                if( vis == visibility_type::CLEAR || vis == visibility_type::LIT ||
                    you.has_memory_at( here.get_abs( n ) ) ) {
                    return true;
                }
            }
        }
        return false;
    }

    uint32_t color_at( const tripoint_bub_ms &p ) const {
        if( !here.inbounds( p ) ) {
            return pack_light( 0, 0, 0 );
        }
        // Debug aid: CATA_R3D_FULLBRIGHT=1 shows all loaded geometry regardless of visibility.
        static const bool fullbright = std::getenv( "CATA_R3D_FULLBRIGHT" ) != nullptr;
        if( fullbright ) {
            return pack_light( 255, 255, 255 );
        }
        if( p.z() > vision_top ) {
            const uint32_t below = color_at( tripoint_bub_ms( p.xy(), vision_top ) );
            if( below != pack_light( 0, 0, 0 ) ) {
                return below;
            }
            return you.has_memory_at( here.get_abs( p ) ) ? remembered_light() : pack_light( 0, 0, 0 );
        }
        if( p.z() < vision_floor ) {
            return you.has_memory_at( here.get_abs( p ) ) ? remembered_light() : pack_light( 0, 0, 0 );
        }
        const lit_level ll = here.access_cache( p.z() ).visibility_cache[p.x()][p.y()];
        const visibility_type vis = here.get_visibility( ll, vvc );
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if( vis == visibility_type::CLEAR || vis == visibility_type::LIT ) {
            switch( ll ) {
                case lit_level::LIT:
                case lit_level::BRIGHT:
                    r = g = b = 1.0f;
                    break;
                // Close to the flat view, which barely dims tiles that are seen at all.
                case lit_level::BRIGHT_ONLY:
                    r = g = b = 0.95f;
                    break;
                case lit_level::LOW:
                    r = g = b = 0.88f;
                    break;
                case lit_level::DARK:
                    r = g = b = 0.8f;
                    break;
                default:
                    break;
            }
        } else if( you.has_memory_at( here.get_abs( p ) ) ) {
            return remembered_light();
        } else if( !here.is_outside( p ) || borders_known( p ) ) {
            // Never seen, but under a roof or next to known ground: marked so that the floor
            // and any wall there can be drawn dark (an unexplored room reads as a dark room,
            // not a hole, and the walls at the edge of the known do not end in mid-air);
            // nothing else on it is.
            return unseen_indoor_light();
        }
        const auto to_byte = []( float v ) {
            return static_cast<uint8_t>( std::clamp( v, 0.0f, 1.0f ) * 255.0f + 0.5f );
        };
        return pack_light( to_byte( r ), to_byte( g ), to_byte( b ) );
    }
};

// Changes whenever the light of any drawn tile may have: a new turn, a moved view, or a fresh
// visibility cache on one of the drawn levels (doors, light sources, ... within a turn).
uint64_t light_key( const map &here, const tripoint_abs_ms &origin, int z_lo, int z_hi )
{
    uint64_t key = static_cast<uint64_t>( to_turn<int>( calendar::turn ) );
    for( int z = z_lo; z <= z_hi; ++z ) {
        key = key * 1099511628211ull ^ here.get_cache_ref( z ).visibility_generation;
    }
    key = key * 1000003u + static_cast<uint64_t>( origin.x() );
    key = key * 1000003u + static_cast<uint64_t>( origin.y() );
    key = key * 1000003u + static_cast<uint64_t>( origin.z() + OVERMAP_DEPTH );
    return key | 1u; // never 0, which means "not computed"
}

void update_chunk_light( state &s, chunk_gpu &c, const tripoint_abs_sm &pos,
                         const tripoint_abs_ms &origin, const tripoint_bub_ms &center,
                         const light_sampler &sampler, uint64_t key )
{
    c.light_key = key;
    if( c.refs.empty() ) {
        return;
    }
    constexpr int border = SEEX + 2;
    const tripoint_abs_ms chunk_origin = project_to<coords::ms>( pos );
    const tripoint_bub_ms base( center.x() + ( chunk_origin.x() - origin.x() ),
                                center.y() + ( chunk_origin.y() - origin.y() ),
                                center.z() + ( pos.z() - origin.z() ) );
    // Light of the tiles in and around the chunk, on its level and the one above.
    std::array<std::array<uint32_t, border *border>, 2> grid;
    for( int dz = 0; dz < 2; ++dz ) {
        for( int ly = -1; ly <= SEEY; ++ly ) {
            for( int lx = -1; lx <= SEEX; ++lx ) {
                grid[dz][static_cast<size_t>( ( ly + 1 ) * border + ( lx + 1 ) )] =
                    sampler.color_at( tripoint_bub_ms( base.x() + lx, base.y() + ly, base.z() + dz ) );
            }
        }
    }
    for( size_t i = 0; i < c.refs.size(); ++i ) {
        const light_ref &r = c.refs[i];
        const int gx = std::clamp( r.lx + r.dx + 1, 0, border - 1 );
        const int gy = std::clamp( r.ly + r.dy + 1, 0, border - 1 );
        uint32_t tile = grid[std::clamp( static_cast<int>( r.dz ), 0, 1 )][static_cast<size_t>
                        ( gy * border + gx )];
        // A side lit by the tile it faces would go black when that tile is out of sight (behind
        // lockers, the far side of a wall) although the thing itself is seen: then it takes its
        // own tile's light.
        if( tile == pack_light( 0, 0, 0 ) && ( r.dx != 0 || r.dy != 0 ) ) {
            tile = grid[0][static_cast<size_t>( ( r.ly + 1 ) * border + ( r.lx + 1 ) )];
        }
        // A never-seen floor or wall is drawn dark (the shader leaves black out), so an
        // unexplored room reads as a dark room, not a hole, and a wall does not stop short of
        // a tile nobody has looked at; nothing else standing there shows.
        if( tile == unseen_indoor_light() ) {
            tile = r.floor || r.wall ? pack_light( 70, 68, 74 ) : pack_light( 0, 0, 0 );
        }
        const uint32_t shade = r.shade;
        c.light[i] = pack_light(
                         static_cast<uint8_t>( ( ( tile >> 0 ) & 255u ) * shade / 255u * r.tint_r / 255u ),
                         static_cast<uint8_t>( ( ( tile >> 8 ) & 255u ) * shade / 255u * r.tint_g / 255u ),
                         static_cast<uint8_t>( ( ( tile >> 16 ) & 255u ) * shade / 255u * r.tint_b / 255u ) );
    }
    s.device.upload( c.lights, c.light.data(),
                     static_cast<uint32_t>( c.light.size() * sizeof( uint32_t ) ), true );
}

// Cuts a hole around the player through whatever stands in front of them (see r3d_block.frag).
void set_cutaway( block_uniforms &u, const mat4 &view_proj, float aspect, const camera &cam,
                  const tripoint_bub_ms &center )
{
    const avatar &you = get_avatar();
    const tripoint_bub_ms p = you.pos_bub();
    // In first person the player is the camera: nothing can stand between.
    if( cam.first_person() || !get_option<bool>( "R3D_CUTAWAY" ) || p.z() != center.z() ) {
        return;
    }
    // Radius in tiles around the player's middle, and the height (above their floor) below which
    // nothing is cut, so the floor and low furniture around them stay.
    constexpr float radius = 1.3f;
    constexpr float keep_below = 0.3f;
    const float base = floor_height;
    const vec3 middle = tile_to_world( { static_cast<float>( p.x() - center.x() ) + 0.5f,
                                         static_cast<float>( p.y() - center.y() ) + 0.5f,
                                         base + cutaway_stub_height } );
    const vec3 at = transform_point( view_proj, middle );
    const vec3 side = transform_point( view_proj, middle + cam.ground_right() * radius );
    u.cut[0] = at.x;
    u.cut[1] = at.y;
    u.cut[2] = at.z;
    u.cut[3] = std::abs( side.x - at.x );
    u.cut2[0] = 1.0f / std::max( aspect, 0.01f );
    u.cut2[1] = base + keep_below;
}

// Follows the R3D_CAMERA_MODE option. Switching keeps the direction the view looks in, so the
// picture does not jump and the movement keys keep their meaning.
void sync_camera_mode( camera &cam )
{
    constexpr float eighth = 0.78539816f;
    const bool want_first_person = get_option<std::string>( "R3D_CAMERA_MODE" ) == "first_person";
    if( want_first_person == cam.first_person() ) {
        return;
    }
    if( want_first_person ) {
        cam.heading = cam.forward_eighths();
        cam.mode = camera_mode::first_person;
        cam.look_pitch = 0.0f;
        cam.glance_yaw = 0.0f;
        cam.glance_pitch = 0.0f;
    } else {
        cam.mode = camera_mode::orbit;
        // The orbit yaw turns the other way round (see camera::forward_eighths).
        cam.yaw = -static_cast<float>( cam.heading ) * eighth;
    }
}

// The first-person heading is kept in the save's UI state (uistate), so a loaded game faces the
// way it was saved. A value there that the renderer did not write itself came from loading a
// game: the view takes it over at once, without easing a turn in.
void sync_saved_heading( state &s )
{
    const int saved = ( uistate.r3d_fp_heading % 8 + 8 ) % 8;
    if( saved != s.fp_heading_saved ) {
        s.cam.heading = saved;
        s.fp_prev_heading = saved;
    }
    if( s.cam.first_person() ) {
        uistate.r3d_fp_heading = s.cam.heading;
    }
    s.fp_heading_saved = ( uistate.r3d_fp_heading % 8 + 8 ) % 8;
}

// Writes a heading the player just chose to the save's UI state, before any frame is drawn.
void remember_heading( state &s )
{
    if( s.cam.first_person() ) {
        uistate.r3d_fp_heading = s.cam.heading;
        s.fp_heading_saved = s.cam.heading;
    }
}

// First person: eases the picture from where the camera was to where the player now is. The
// game state (position, heading, picking) always has the end values; only what is drawn lags, for
// at most R3D_FP_TWEEN_MS. Sets the offsets to add to the camera this frame and the yaw to show.
// surface is the height the player stands at within their level (floor, counter top): taking
// stairs, or climbing onto a counter, eases the eye up or down as well (offset_z, in levels).
void update_fp_ease( state &s, const tripoint_abs_ms &pos, float surface, float &offset_x,
                     float &offset_y, float &offset_z )
{
    constexpr float eighth = 0.78539816f;
    avatar &you = get_avatar();
    const uint32_t now = static_cast<uint32_t>( SDL_GetTicks() );
    uint32_t length = static_cast<uint32_t>( std::max( 0, get_option<int>( "R3D_FP_TWEEN_MS" ) ) );
    // Travelling and activities make many moves in a row; showing each would only slow them.
    if( you.is_auto_moving() || you.activity || !get_option<bool>( "ANIMATIONS" ) ) {
        length = 0;
    }
    // What is still to be shown of the ease in progress: it fades out cubically.
    auto remaining = [ &s, now ]() {
        if( s.fp_ease_ms == 0 || now - s.fp_ease_start >= s.fp_ease_ms ) {
            return 0.0f;
        }
        const float left = 1.0f - static_cast<float>( now - s.fp_ease_start ) /
                           static_cast<float>( s.fp_ease_ms );
        return left * left * left;
    };
    if( s.fp_prev_valid && length > 0 ) {
        const tripoint_rel_ms step = pos - s.fp_prev_pos;
        // How far the eye rises: a storey for stairs, plus the change in what is stood on.
        const float rise = static_cast<float>( step.z() ) + surface - s.fp_prev_surface;
        // A step to a neighbouring tile, up or down stairs (which may land on a tile next to
        // the one above or below), or onto furniture; teleports and falls are not eased.
        const bool stepped = ( step.x() != 0 || step.y() != 0 || rise != 0.0f ) &&
                             std::abs( step.x() ) <= 1 && std::abs( step.y() ) <= 1 &&
                             std::abs( step.z() ) <= 1;
        const int turned = ( ( s.cam.heading - s.fp_prev_heading + 12 ) % 8 ) - 4;
        if( stepped || turned != 0 ) {
            // Carry over what is left, so a held key flows from one step into the next.
            const float left = remaining();
            s.fp_ease_dx = s.fp_ease_dx * left - ( stepped ? static_cast<float>( step.x() ) : 0.0f );
            s.fp_ease_dy = s.fp_ease_dy * left - ( stepped ? static_cast<float>( step.y() ) : 0.0f );
            s.fp_ease_dz = s.fp_ease_dz * left - ( stepped ? rise : 0.0f );
            s.fp_ease_yaw = s.fp_ease_yaw * left - static_cast<float>( turned ) * eighth;
            s.fp_ease_start = now;
            s.fp_ease_ms = length;
        }
    } else if( length == 0 ) {
        s.fp_ease_ms = 0;
    }
    s.fp_prev_valid = true;
    s.fp_prev_pos = pos;
    s.fp_prev_surface = surface;
    s.fp_prev_heading = s.cam.heading;
    const float left = remaining();
    s.fp_easing = left > 0.0f;
    offset_x = s.fp_ease_dx * left;
    offset_y = s.fp_ease_dy * left;
    offset_z = s.fp_ease_dz * left;
    s.cam.tween_yaw = s.fp_ease_yaw * left;
}

void apply_env_camera( camera &cam )
{
    // CATA_R3D_CAMERA="yaw_deg,pitch_deg,distance[,ortho[,fp_heading[,fp_pitch_deg]]]" gives
    // repeatable views for screenshots; ortho 1 switches to an orthographic projection.
    // "-45,30,20,1" matches the flat isometric view: looking north-east at 30 degrees, which
    // draws tiles as 2:1 diamonds. The last two fields set the first-person facing (in eighths
    // of a turn) and look angle; they only matter when R3D_CAMERA_MODE is first_person.
    const char *spec = std::getenv( "CATA_R3D_CAMERA" );
    if( !spec ) {
        return;
    }
    float yaw = 0.0f;
    float pitch = 0.0f;
    float dist = 0.0f;
    int ortho = 0;
    int fp_heading = 0;
    float fp_pitch = 0.0f;
    const int n = std::sscanf( spec, "%f,%f,%f,%d,%d,%f", &yaw, &pitch, &dist, &ortho, &fp_heading,
                               &fp_pitch );
    if( n >= 3 ) {
        constexpr float deg = 0.017453293f;
        cam.yaw = yaw * deg;
        cam.pitch = std::clamp( pitch * deg, camera::min_pitch, camera::max_pitch );
        cam.distance = std::clamp( dist, camera::min_distance, camera::max_distance );
        if( n >= 4 ) {
            cam.orthographic = ortho != 0;
        }
        if( n >= 5 ) {
            cam.heading = ( fp_heading % 8 + 8 ) % 8;
        }
        if( n >= 6 ) {
            cam.look_pitch = std::clamp( fp_pitch * deg, -camera::max_look_pitch,
                                         camera::max_look_pitch );
        }
    }
}

void maybe_take_screenshot( state &s )
{
    // CATA_R3D_SCREENSHOT=<path> saves the finished previous frame once things have settled;
    // with CATA_R3D_QUIT_AFTER_SHOT set the game then exits, for scripted visual checks.
    const char *path = std::getenv( "CATA_R3D_SCREENSHOT" );
    if( !path || s.screenshot_taken || s.frame < 40 ) {
        return;
    }
    s.screenshot_taken = true;
    save_screenshot( path );
    if( std::getenv( "CATA_R3D_QUIT_AFTER_SHOT" ) ) {
        std::_Exit( 0 );
    }
}

float picking_column_height( state &s, const shown_view &v, const map &here,
                             const creature_tracker &creatures, const avatar &you, int dx, int dy, int dz )
{
    const tripoint_bub_ms p( v.center.x() + dx, v.center.y() + dy, v.center.z() + dz );
    if( p.z() < v.z_lo || p.z() > v.z_hi || !here.inbounds( p ) ) {
        return 0.0f;
    }
    const float h = column_height( s, here, p );
    const Creature *critter = creatures.creature_at( p );
    // In first person the viewer's own column would block every ray from their eye.
    if( critter && ( critter->is_avatar() ? !v.cam.first_person() : you.sees( here, *critter ) ) ) {
        return 1.0f;
    }
    return h;
}

const submap *loaded_submap_at( const tripoint_abs_sm &base, int view_z, bool multi_z,
                                const tripoint_abs_sm &p )
{
    const int dx = p.x() - base.x();
    const int dy = p.y() - base.y();
    if( dx < 0 || dy < 0 || dx >= MAPSIZE || dy >= MAPSIZE ||
        p.z() < -OVERMAP_DEPTH || p.z() > OVERMAP_HEIGHT ) {
        return nullptr;
    }
    if( p.z() != view_z && !multi_z ) {
        return nullptr;
    }
    return MAPBUFFER.lookup_submap( p );
}

void append_frame_markers( state &s, const map &here, entity_view &entities_view,
                           const std::vector<tripoint_bub_ms> &where, const std::string &id,
                           bool box, bool large )
{
    const tile_info &sprite = s.cache.named( TILE_CATEGORY::NONE, id, *tilecontext );
    for( const tripoint_bub_ms &p : where ) {
        entities_view.markers.push_back( { p, column_height( s, here, p ), &sprite, box, large, false } );
    }
}

float occluding_column_height( state &s, const map &here, const light_sampler &sampler,
                               const tripoint_bub_ms &center, int z_lo, int z_hi, bool occlude_unseen, int dx, int dy, int dz )
{
    const tripoint_bub_ms c( center.x() + dx, center.y() + dy, center.z() + dz );
    if( c.z() < z_lo || c.z() > z_hi || !here.inbounds( c ) ) {
        return 0.0f;
    }
    if( !occlude_unseen && sampler.color_at( c ) == pack_light( 0, 0, 0 ) ) {
        return 0.0f;
    }
    return column_height( s, here, c );
}

} // namespace

namespace
{
// Reloads the map tileset after its option changed, as the options menu does.
bool reload_map_tileset()
{
    try {
        closetilecontext->reinit();
        closetilecontext->load_tileset( get_option<std::string>( "TILES" ), /*precheck=*/false,
                                        /*force=*/false, /*pump_events=*/true, /*terrain=*/false );
        g->reset_zoom();
        g->mark_main_ui_adaptor_resize();
        return true;
    } catch( const std::exception &e ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d compare: loading tileset " <<
                                    get_option<std::string>( "TILES" ) << " failed: " << e.what();
        return false;
    }
}

// Draws the whole screen into the renderer and saves it before it is presented.
bool capture( const std::string &path )
{
    g->invalidate_main_ui_adaptor();
    ui_manager::redraw();
    const bool ok = save_screenshot( path );
    DebugLog( ok ? D_INFO : D_ERROR, DC_ALL ) << "r3d compare: " << ( ok ? "saved " : "failed " ) <<
            path;
    return ok;
}
// Writes what the shots show as text: the player and the camera (orbit and first person), and
// the tiles around the view (furniture symbol over terrain symbol, and whether each is seen,
// remembered or unknown), so something odd in a picture can be traced to a map tile.
void dump_view( const std::string &path, const tripoint_bub_ms &center )
{
    std::ofstream out( path );
    if( !out ) {
        return;
    }
    constexpr float deg = 57.29578f;
    const map &here = get_map();
    const avatar &you = get_avatar();
    const state &s = st();
    const camera &cam = s.cam;
    out << "player bub " << you.pos_bub().to_string() << " abs " << you.pos_abs().to_string()
        << "\nview center bub " << center.to_string()
        << "\ncamera mode " << ( cam.first_person() ? "first_person" : "orbit" )
        << "\norbit yaw " << cam.yaw << " pitch " << cam.pitch << " distance " << cam.distance
        << " ortho " << cam.orthographic << " pan " << s.pan.x << "," << s.pan.y << "," << s.pan.z
        << " z_scale " << cam.z_scale << "\n";
    if( cam.first_person() ) {
        static const char *const names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        const vec3 eye = cam.eye();
        const float aspect = s.shown.valid && s.shown.h > 0.0f ? s.shown.w / s.shown.h : 1.0f;
        out << "first person heading " << cam.heading << " (" << names[( cam.heading % 8 + 8 ) % 8]
            << ")\n  eye (stretched world, scene frame: x east, y north, z up) " << eye.x << ","
            << eye.y << "," << eye.z << "\n  eye height " << cam.eye_height << " yaw "
            << cam.fp_yaw() * deg << " deg, look pitch " << cam.look_pitch *deg << " + tilt "
            << cam.tilt_pitch *deg << " + glance " << cam.glance_pitch *deg << " deg\n  fov x "
            << cam.fp_fovx( aspect ) * deg << " deg, fov y " << cam.fp_fovy( aspect ) * deg
            << " deg at aspect "
            << aspect << "\n";
    }
    constexpr int half_w = 16;
    constexpr int half_h = 12;
    const visibility_variables &vvc = here.get_visibility_variables_cache();
    std::map<std::string, std::string> legend;
    out << "\nmap (x " << center.x() - half_w << ".." << center.x() + half_w << ", y "
        << center.y() - half_h << ".." << center.y() + half_h << ", north up, @ = player):\n";
    for( int dy = -half_h; dy <= half_h; ++dy ) {
        for( int dx = -half_w; dx <= half_w; ++dx ) {
            const tripoint_bub_ms p = center + tripoint_rel_ms( dx, dy, 0 );
            if( !here.inbounds( p ) ) {
                out << ' ';
                continue;
            }
            if( p == you.pos_bub() ) {
                out << '@';
                continue;
            }
            const furn_id f = here.furn( p );
            const ter_id t = here.ter( p );
            const int sym = f != furn_str_id::NULL_ID() ? f->symbol() : t->symbol();
            const std::string id = f != furn_str_id::NULL_ID() ? f.id().str() : t.id().str();
            legend[std::string( 1, static_cast<char>( sym ) )] = id;
            out << static_cast<char>( sym );
        }
        out << '\n';
    }
    out << "\nvisibility (S seen, M remembered, . unknown):\n";
    for( int dy = -half_h; dy <= half_h; ++dy ) {
        for( int dx = -half_w; dx <= half_w; ++dx ) {
            const tripoint_bub_ms p = center + tripoint_rel_ms( dx, dy, 0 );
            if( !here.inbounds( p ) ) {
                out << ' ';
                continue;
            }
            const lit_level ll = here.access_cache( p.z() ).visibility_cache[p.x()][p.y()];
            const visibility_type vis = here.get_visibility( ll, vvc );
            const bool seen = vis == visibility_type::CLEAR || vis == visibility_type::LIT;
            out << ( seen ? 'S' : you.has_memory_at( here.get_abs( p ) ) ? 'M' : '.' );
        }
        out << '\n';
    }
    out << "\nitems (tiles whose items the player sees; top item and count):\n";
    for( int dy = -half_h; dy <= half_h; ++dy ) {
        for( int dx = -half_w; dx <= half_w; ++dx ) {
            const tripoint_bub_ms p = center + tripoint_rel_ms( dx, dy, 0 );
            if( !here.inbounds( p ) || !here.sees_some_items( p, you ) ) {
                continue;
            }
            const map_stack items = get_map().i_at( p );
            if( items.empty() ) {
                continue;
            }
            out << "  (" << p.x() << ',' << p.y() << ") " << items.begin()->tname() << " x"
                << items.size() << '\n';
        }
    }
    out << "\nlegend (last id drawn with each symbol):\n";
    for( const auto &e : legend ) {
        out << "  " << e.first << "  " << e.second << '\n';
    }
}

// Draws frames until the chunks around the view are built (a few per frame). The game is
// waiting for input and draws nothing by itself. Each frame is presented, as the game always
// does: frames drawn without a present pile up in the renderer, and after about eighty of them
// lavapipe crashed reading the next shot back.
void settle_frames()
{
    constexpr int frames = 40;
    const SDL_Renderer_Ptr &r = get_sdl_renderer();
    for( int i = 0; i < frames; ++i ) {
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw();
        if( r ) {
            SDL_RenderPresent( r.get() );
        }
    }
    // The shot is read back from the next frame, drawn by capture().
}

} // namespace

void compare_capture()
{
    // CATA_R3D_COMPARE=<prefix> saves, once, as soon as a game is on screen:
    //  - <prefix>_2d.png: the flat view with the isometric tileset CATA_R3D_COMPARE_TILESET
    //    (default Ultica_iso);
    //  - <prefix>_3d.png: the same turn and place in the 3D view with tileset
    //    CATA_R3D_COMPARE_3D_TILESET (default UltimateCataclysm) through the CATA_R3D_CAMERA
    //    camera (e.g. "-45,30,20,1"); an orthographic camera gets the isometric shot's scale.
    // No input is handled in between, so no turn passes. Afterwards the previous tileset and
    // view come back (the options file is never written), or with CATA_R3D_QUIT_AFTER_SHOT the
    // game exits (status 0 when both shots were saved).
    static const char *const prefix = std::getenv( "CATA_R3D_COMPARE" );
    static bool done = false;
    if( !prefix || done || !g || !closetilecontext || !st().map_drawn ) {
        return;
    }
    done = true;
    const std::string base = prefix;
    options_manager::cOpt &tiles = get_options().get_option( "TILES" );
    options_manager::cOpt &view_3d = get_options().get_option( "RENDERER_3D" );
    const std::string old_tiles = tiles.getValue();
    const std::string old_view_3d = view_3d.getValue();
    const auto env_or = []( const char *name, const char *fallback ) {
        const char *const value = std::getenv( name );
        return std::string( value ? value : fallback );
    };

    // The flat isometric view first: its tile size sets the scale of the 3D shot.
    tiles.setValue( env_or( "CATA_R3D_COMPARE_TILESET", "Ultica_iso" ) );
    view_3d.setValue( "false" );
    const bool ok_2d = reload_map_tileset() && capture( base + "_2d.png" );
    const int iso_tile_width = closetilecontext->get_tile_width();

    tiles.setValue( env_or( "CATA_R3D_COMPARE_3D_TILESET", "UltimateCataclysm" ) );
    view_3d.setValue( "true" );
    bool ok_3d = reload_map_tileset();
    if( ok_3d ) {
        // The game is waiting for input and draws nothing by itself; draw until the chunks
        // around the view are built (a few per frame).
        const auto redraw = []() {
            g->invalidate_main_ui_adaptor();
            ui_manager::redraw();
        };
        redraw();
        // The renderer only uploads the new tileset's textures when it presents, and the
        // capture reads the 3D pass before any present: show one frame so the sheets the
        // meshes sample from are on the GPU (SDL versions differ in when a flush uploads).
        if( const SDL_Renderer_Ptr &r = get_sdl_renderer() ) {
            SDL_RenderPresent( r.get() );
        }
        state &s = st();
        if( s.cam.orthographic && iso_tile_width > 0 && s.shown.valid ) {
            // Same scale as the flat view: a tile (whose diagonal spans sqrt(2) across the
            // screen at the isometric yaw) as wide as an isometric diamond.
            const float half_height = 1.41421356f * s.shown.h / ( 2.0f * iso_tile_width );
            s.cam.distance = std::clamp( half_height / std::tan( s.cam.fovy * 0.5f ),
                                         camera::min_distance, camera::max_distance );
        }
        constexpr int settle_frames = 40;
        for( int i = 0; i < settle_frames; ++i ) {
            redraw();
        }
        ok_3d = active() && capture( base + "_3d.png" );
        dump_view( base + "_map.txt", g->ter_view_p );
    }

    if( std::getenv( "CATA_R3D_QUIT_AFTER_SHOT" ) ) {
        std::_Exit( ok_3d && ok_2d ? 0 : 1 );
    }
    tiles.setValue( old_tiles );
    view_3d.setValue( old_view_3d );
    reload_map_tileset();
    g->invalidate_main_ui_adaptor();
}

void set_hud_font( int cell_width, int cell_height )
{
    state &s = st();
    s.hud_cell_w = std::max( 1, cell_width );
    s.hud_cell_h = std::max( 1, cell_height );
}

const std::vector<hud_text> &hud_texts()
{
    return st().hud;
}

void fp_shots_capture()
{
    // CATA_R3D_FP_SHOTS=<prefix> saves, once, as soon as a game is on screen, with no input
    // handled in between (so no turn passes):
    //  - <prefix>_orbit.png: the 3D view through the orbit camera, for reference;
    //  - <prefix>_fp_<n|ne|e|se|s|sw|w|nw>.png: the whole window in first person facing each of
    //    the eight headings, HUD included;
    //  - <prefix>_map.txt: the player, both cameras and the tiles around them (see dump_view).
    // CATA_R3D_FP_TILESET picks the tileset (default: the current one), CATA_R3D_FP_LOOK=up,
    // down or level (default) tilts the first-person shots by 45 degrees. Afterwards the
    // tileset, the 3D view, the camera mode and the heading come back (the options file is
    // never written), or with CATA_R3D_QUIT_AFTER_SHOT the game exits (0 when every shot saved).
    static const char *const prefix = std::getenv( "CATA_R3D_FP_SHOTS" );
    static bool done = false;
    if( !prefix || done || !g || !closetilecontext || !st().map_drawn ) {
        return;
    }
    done = true;
    const std::string base = prefix;
    options_manager::cOpt &tiles = get_options().get_option( "TILES" );
    options_manager::cOpt &view_3d = get_options().get_option( "RENDERER_3D" );
    options_manager::cOpt &mode = get_options().get_option( "R3D_CAMERA_MODE" );
    const std::string old_tiles = tiles.getValue();
    const std::string old_view_3d = view_3d.getValue();
    const std::string old_mode = mode.getValue();
    state &s = st();
    // The whole camera: switching modes turns the orbit camera to the heading and back.
    const camera old_cam = s.cam;
    const int old_saved_heading = uistate.r3d_fp_heading;

    bool ok = true;
    const char *const tileset = std::getenv( "CATA_R3D_FP_TILESET" );
    const bool swap_tileset = tileset && old_tiles != tileset;
    view_3d.setValue( "true" );
    mode.setValue( "orbit" );
    if( swap_tileset ) {
        tiles.setValue( tileset );
        ok = reload_map_tileset();
    }
    // The settling frames are presented, which also puts a new tileset's textures on the GPU
    // (the renderer only uploads them when it presents) before the shot reads the 3D pass.
    settle_frames();
    // Every shot is attempted even after one failed, so the files show what went wrong.
    ok = active() && capture( base + "_orbit.png" ) && ok;

    mode.setValue( "first_person" );
    // The first frame switches the camera (which levels the view); the tilt is set after it.
    settle_frames();
    const std::string look = std::getenv( "CATA_R3D_FP_LOOK" ) ? std::getenv( "CATA_R3D_FP_LOOK" ) :
                             "level";
    constexpr float level_tilt = 0.7854f;
    s.cam.look_pitch = look == "up" ? level_tilt : look == "down" ? -level_tilt : 0.0f;
    static const char *const names[8] = { "n", "ne", "e", "se", "s", "sw", "w", "nw" };
    for( int h = 0; h < 8; ++h ) {
        s.cam.heading = h;
        end_glance();
        // No easing between the shots, whatever R3D_FP_TWEEN_MS says.
        s.fp_prev_valid = false;
        s.fp_ease_ms = 0;
        settle_frames();
        ok = capture( base + "_fp_" + names[h] + ".png" ) && first_person() && ok;
        if( h == 0 ) {
            dump_view( base + "_map.txt", g->ter_view_p );
        }
    }
    DebugLog( ok ? D_INFO : D_ERROR, DC_ALL ) << "r3d fp shots: " << ( ok ? "all saved" :
            "some shots failed" );

    if( std::getenv( "CATA_R3D_QUIT_AFTER_SHOT" ) ) {
        std::_Exit( ok ? 0 : 1 );
    }
    s.cam = old_cam;
    uistate.r3d_fp_heading = old_saved_heading;
    s.fp_heading_saved = ( old_saved_heading % 8 + 8 ) % 8;
    s.fp_prev_valid = false;
    mode.setValue( old_mode );
    view_3d.setValue( old_view_3d );
    if( swap_tileset ) {
        tiles.setValue( old_tiles );
        reload_map_tileset();
    }
    g->invalidate_main_ui_adaptor();
}

bool active()
{
    return st().drawn && get_option<bool>( "RENDERER_3D" );
}

int view_eighths()
{
    const state &s = st();
    if( !active() || !s.shown.valid ) {
        return 0;
    }
    // The first-person heading changes before the next frame is drawn; the orbit camera is read
    // as last shown.
    return s.shown.cam.first_person() ? s.cam.heading : s.shown.cam.forward_eighths();
}

bool animating()
{
    const state &s = st();
    return s.fp_easing && active() && s.shown.valid && s.shown.cam.first_person();
}

bool first_person()
{
    const state &s = st();
    return active() && s.shown.valid && s.shown.cam.first_person();
}

bool wasd_controls()
{
    return active() && get_option<bool>( "R3D_WASD" );
}

void toggle()
{
    get_options().get_option( "RENDERER_3D" ).setNext();
    get_options().save();
}

void toggle_first_person()
{
    get_options().get_option( "R3D_CAMERA_MODE" ).setNext();
    get_options().save();
}

void end_glance()
{
    camera &cam = st().cam;
    cam.glance_yaw = 0.0f;
    cam.glance_pitch = 0.0f;
}

std::optional<tripoint_bub_ms> crosshair()
{
    const shown_view &v = st().shown;
    if( !first_person() ) {
        return std::nullopt;
    }
    return pick( v.x + v.w * 0.5f, v.y + v.h * 0.5f );
}

void face_towards( int dx, int dy )
{
    state &s = st();
    if( s.cam.first_person() && ( dx != 0 || dy != 0 ) ) {
        s.cam.heading = eighths_towards( dx, dy );
        remember_heading( s );
        end_glance();
    }
}

void rotate( int dir )
{
    state &s = st();
    s.cam.rotate_steps( dir );
    if( s.cam.first_person() ) {
        remember_heading( s );
        end_glance();
    }
}

void pitch( int dir )
{
    st().cam.pitch_steps( dir );
}

void zoom( int dir )
{
    camera &cam = st().cam;
    if( cam.first_person() ) {
        // There is no distance to change: the same keys look one level up or down, which is a
        // 45 degree tilt of the view and back.
        constexpr float level_tilt = 0.7854f;
        const int now = cam.look_pitch > 0.4f ? 1 : cam.look_pitch < -0.4f ? -1 : 0;
        cam.look_pitch = static_cast<float>( std::clamp( now + ( dir > 0 ? 1 : -1 ), -1, 1 ) ) *
                         level_tilt;
        return;
    }
    cam.zoom_steps( dir );
}

void reset()
{
    state &s = st();
    // First person keeps its mode and heading: resetting only straightens the view.
    const camera_mode mode = s.cam.mode;
    const int heading = s.cam.heading;
    s.cam = camera();
    s.cam.mode = mode;
    s.cam.heading = heading;
    s.pan = vec3{};
    s.env_camera_applied = false;
}

bool contains( float x, float y )
{
    const shown_view &v = st().shown;
    return active() && v.valid && x >= v.x && y >= v.y && x < v.x + v.w && y < v.y + v.h;
}

void drag_orbit( float dx, float dy )
{
    camera &cam = st().cam;
    constexpr float yaw_per_pixel = 0.008f;
    constexpr float pitch_per_pixel = 0.006f;
    constexpr float two_pi = 6.2831853f;
    if( cam.first_person() ) {
        // A glance: looks around for as long as the button is held, then snaps back to the
        // heading. The mouse follows the view (right looks right, down looks down).
        constexpr float max_glance_yaw = 2.4f;
        cam.glance_yaw = std::clamp( cam.glance_yaw + dx * yaw_per_pixel, -max_glance_yaw,
                                     max_glance_yaw );
        cam.glance_pitch = std::clamp( cam.glance_pitch - dy * pitch_per_pixel,
                                       -camera::max_look_pitch - cam.look_pitch,
                                       camera::max_look_pitch - cam.look_pitch );
        return;
    }
    cam.yaw = std::fmod( cam.yaw - dx * yaw_per_pixel, two_pi );
    cam.pitch = std::clamp( cam.pitch + dy * pitch_per_pixel, camera::min_pitch, camera::max_pitch );
}

void drag_pan( float dx, float dy )
{
    state &s = st();
    if( s.cam.first_person() ) {
        return; // the eye stays on the player
    }
    // The ground under the cursor follows the mouse: farther cameras move faster.
    const float units_per_pixel = s.cam.distance * 0.0016f;
    const vec3 right = s.cam.ground_right();
    const vec3 ahead{ -right.y, right.x, 0.0f }; // away from the camera, along the ground
    s.pan = s.pan + right * ( -dx * units_per_pixel ) + ahead * ( dy * units_per_pixel );
    // Stay over the loaded map around the view.
    constexpr float max_pan = 48.0f;
    s.pan.x = std::clamp( s.pan.x, -max_pan, max_pan );
    s.pan.y = std::clamp( s.pan.y, -max_pan, max_pan );
}

std::optional<tripoint_bub_ms> pick( float x, float y )
{
    state &s = st();
    const shown_view &v = s.shown;
    if( !active() || !v.valid || v.w <= 0.0f || v.h <= 0.0f || !tilecontext ) {
        return std::nullopt;
    }
    if( x < v.x || y < v.y || x >= v.x + v.w || y >= v.y + v.h ) {
        return std::nullopt;
    }
    map &here = get_map();
    const avatar &you = get_avatar();
    const creature_tracker &creatures = get_creature_tracker();
    // Visible creatures count as a full-height column so clicking their sprite picks their tile.
    const column_height_fn height = [ &v, &here, &s, &creatures, &you ]( int dx, int dy, int dz ) {
        return picking_column_height( s, v, here, creatures, you, dx, dy, dz );
    };
    const float ndc_x = ( x - v.x ) / v.w * 2.0f - 1.0f;
    const float ndc_y = 1.0f - ( y - v.y ) / v.h * 2.0f;
    constexpr float max_pick_distance = 400.0f;
    const voxel_hit hit = pick_cell( v.cam, ndc_x, ndc_y, v.w / v.h, max_pick_distance, height,
                                     floor_height );
    if( !hit.hit ) {
        return std::nullopt;
    }
    // Callers expect the level of the 2D view, so a hit seen through a hole on a lower level
    // becomes the tile where the ray crossed that level (the hole), not the floor below it.
    int px = hit.x;
    int py = hit.y;
    if( hit.z < 0 ) {
        const ray world = v.cam.screen_ray( ndc_x, ndc_y, v.w / v.h );
        const vec3 origin = world_to_tile( world.origin );
        const vec3 dir = world_to_tile( world.dir );
        if( dir.z < 0.0f ) {
            const vec3 cross = origin + dir * ( ( floor_height - origin.z ) / dir.z );
            px = static_cast<int>( std::floor( cross.x ) );
            py = static_cast<int>( std::floor( cross.y ) );
        }
    }
    const tripoint_bub_ms p( v.center.x() + px, v.center.y() + py, here.get_abs_sub().z() );
    if( !here.inbounds( p ) ) {
        return std::nullopt;
    }
    return p;
}

void shutdown()
{
    state &s = st();
    release_all_chunks( s );
    release_dynamic( s );
    s.cache.set_wall_sides( nullptr );
    s.walls_on = false;
    s.tileset_loads = 0;
    s.device.shutdown();
    s.epoch_valid = false;
    s.drawn = false;
    s.shown.valid = false;
}

namespace
{
// First-person overlay, drawn with the 2D renderer over the finished picture: a compass strip
// (the game keeps full-circle vision, so this shows what is behind you), an alert on the edge
// nearest an adjacent hostile that is out of the picture, and a crosshair with its label.
// Bearings follow r3d_hud.h: radians clockwise from north, relative to the view's yaw.
void draw_fp_hud( state &s, SDL_Renderer *renderer, float x, float y, float w, float h,
                  const camera &cam )
{
    constexpr float pi = 3.14159265f;
    // The compass strip shows half a turn: the view in the middle, what is beside you at the ends.
    constexpr float compass_span = pi;
    const std::string compass = get_option<std::string>( "R3D_FP_COMPASS" );
    const bool edge_alert = get_option<bool>( "R3D_FP_EDGE_ALERT" );
    const avatar &you = get_avatar();
    map &here = get_map();
    const float view_yaw = cam.fp_yaw();
    auto bearing_to = [ &you, view_yaw ]( const tripoint_bub_ms & at ) {
        const tripoint_rel_ms d = at - you.pos_bub();
        return relative_bearing( static_cast<float>( d.x() ), static_cast<float>( d.y() ), view_yaw );
    };
    auto fill = [ renderer ]( float rx, float ry, float rw, float rh, uint8_t r, uint8_t g, uint8_t b,
    uint8_t a ) {
        SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
        SDL_SetRenderDrawColor( renderer, r, g, b, a );
        const SDL_FRect rect{ rx, ry, rw, rh };
        SDL_RenderFillRect( renderer, &rect );
    };
    // Labels are drawn by the caller in the game's font (see hud_texts), centred on tx.
    auto text = [ &s ]( float tx, float ty, const std::string & str, unsigned char color ) {
        s.hud.push_back( { tx, ty, str, color, true } );
    };
    constexpr unsigned char white = 15;
    constexpr unsigned char light_gray = 7;
    const float cell_h = static_cast<float>( s.hud_cell_h );

    if( compass != "off" ) {
        const float strip_w = std::max( 120.0f, w * 0.4f );
        // Room for one line of text above the ticks' footing.
        const float strip_h = std::max( 22.0f, cell_h + 8.0f );
        const float left = x + ( w - strip_w ) * 0.5f;
        const float top = y + 6.0f;
        const float mid = left + strip_w * 0.5f;
        auto strip_x = [ mid, strip_w, compass_span ]( float rel ) {
            return mid + compass_offset( rel, strip_w, compass_span );
        };
        fill( left, top, strip_w, strip_h, 0, 0, 0, 110 );
        static const char *const names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        for( int i = 0; i < 8; ++i ) {
            const float rel = wrap_angle( heading_bearing( i ) - view_yaw );
            if( !on_compass( rel, compass_span ) ) {
                continue;
            }
            const std::string label = names[i];
            const bool cardinal = label.size() == 1;
            text( strip_x( rel ), top + 1.0f, label, cardinal ? white : light_gray );
        }
        fill( mid - 1.0f, top + strip_h - 5.0f, 2.0f, 5.0f, 255, 255, 255, 255 );
        // Sounds heard this turn whose post cannot be seen: a short yellow tick each, dimmer
        // when they lie behind the strip's ends. Those in sight are posts on their tile.
        for( const tripoint_bub_ms &p : s.heard_ticks ) {
            const float rel = bearing_to( p );
            fill( strip_x( rel ) - 2.0f, top + strip_h - 9.0f, 4.0f, 9.0f, 240, 210, 60,
                  on_compass( rel, compass_span ) ? 255 : 150 );
        }
        if( compass == "threats" ) {
            for( Creature *critter : you.get_hostile_creatures( MAX_VIEW_DISTANCE ) ) {
                if( critter->posz() != you.posz() ) {
                    continue;
                }
                const float rel = bearing_to( critter->pos_bub() );
                const int distance = std::max( 1, rl_dist( you.pos_bub(), critter->pos_bub() ) );
                // Behind the player the tick sits at the end of the strip it is nearer to.
                const float tick_h = std::clamp( 24.0f / static_cast<float>( distance ), 5.0f, strip_h );
                const float tick_w = distance == 1 ? 5.0f : 3.0f;
                fill( strip_x( rel ) - tick_w * 0.5f, top + strip_h - tick_h, tick_w, tick_h, 230, 40, 40,
                      on_compass( rel, compass_span ) ? 255 : 150 );
            }
        }
    }

    if( edge_alert ) {
        constexpr float bar = 6.0f;
        for( Creature *critter : you.get_hostile_creatures( 1 ) ) {
            if( critter->posz() != you.posz() ) {
                continue;
            }
            switch( edge_alert_side( bearing_to( critter->pos_bub() ), cam.fp_fovx( w / h ) ) ) {
                case edge_side::none:
                    break;
                case edge_side::behind:
                    fill( x, y + h - bar, w, bar, 230, 40, 40, 200 );
                    break;
                case edge_side::left:
                    fill( x, y, bar, h, 230, 40, 40, 200 );
                    break;
                case edge_side::right:
                    fill( x + w - bar, y, bar, h, 230, 40, 40, 200 );
                    break;
            }
        }
    }

    // Look mode on another level: say which, under the compass strip.
    const int looking = you.view_offset.z();
    if( looking != 0 ) {
        const int levels = std::abs( looking );
        const std::string line = looking > 0 ?
                                 string_format( n_gettext( "Looking: %d level up", "Looking: %d levels up",
                                                levels ), levels ) :
                                 string_format( n_gettext( "Looking: %d level down", "Looking: %d levels down",
                                                levels ), levels );
        constexpr unsigned char light_cyan = 14;
        text( x + w * 0.5f, y + std::max( 22.0f, cell_h + 8.0f ) + 10.0f, line, light_cyan );
    }

    if( get_option<bool>( "R3D_FP_CROSSHAIR" ) ) {
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        fill( cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, 255, 255, 255, 200 );
        std::string label;
        const std::optional<tripoint_bub_ms> at = crosshair();
        // Only what the player sees or remembers is named: a black, unexplored wall stays unknown.
        if( at && ( you.sees( here, *at ) || you.has_memory_at( here.get_abs( *at ) ) ) ) {
            const Creature *critter = get_creature_tracker().creature_at( *at );
            if( critter && you.sees( here, *critter ) ) {
                label = critter->disp_name();
            } else if( here.furn( *at ) != furn_str_id::NULL_ID() ) {
                label = here.furnname( *at );
            } else {
                label = here.tername( *at );
            }
        }
        if( !label.empty() ) {
            text( cx, cy + 8.0f, label, white );
        }
    }
}

} // namespace

bool draw( SDL_Renderer *renderer, float x, float y, float w, float h,
           const tripoint_bub_ms &center, unsigned render_epoch )
{
    state &s = st();
    s.drawn = false;
    s.map_drawn = true;
    s.hud.clear();
    if( !get_option<bool>( "RENDERER_3D" ) ) {
        if( s.device.ready() ) {
            shutdown();
        }
        return false;
    }
    if( !tilecontext || !tilecontext->is_valid() ) {
        return false;
    }

    // Anything the renderer rebuilt (device loss, software fallback, texture reupload) invalidates
    // every GPU object we hold, so start over against the new renderer.
    if( !s.device.ready() || s.device.renderer() != renderer || !s.epoch_valid ||
        s.epoch != render_epoch ) {
        if( s.init_failed && s.failed_epoch == render_epoch ) {
            return false;
        }
        shutdown();
        s.init_failed = false;
        if( !s.device.init( renderer ) ) {
            s.init_failed = true;
            s.failed_epoch = render_epoch;
            return false;
        }
        s.epoch = render_epoch;
        s.epoch_valid = true;
    }

    // The wall-side rules and atlas are always loaded (they name each wall's material); the
    // option only decides whether painted walls take the atlas plaster or their own sprite.
    // Switching it rebuilds every chunk.
    if( !s.walls_on ) {
        s.cache.set_wall_sides( load_wall_sides( s.device ) );
        s.walls_on = true;
    }
    const bool want_thin_walls = get_option<bool>( "R3D_THIN_WALLS" );
    if( want_thin_walls != s.thin_walls ) {
        release_all_chunks( s );
        s.cache.set_thin_walls( want_thin_walls );
        s.thin_walls = want_thin_walls;
    }
    const bool want_atlas = get_option<bool>( "R3D_WALL_TEXTURES" );
    if( want_atlas != s.plaster_atlas ) {
        release_all_chunks( s );
        s.cache.set_plaster_atlas( want_atlas );
        s.plaster_atlas = want_atlas;
    }

    // A tileset (re)load swaps the sheet textures and may lay the atlas out differently
    // without bumping the epoch: everything cached from the old sheets is dropped.
    if( tilecontext->tileset_loads() != s.tileset_loads ) {
        release_all_chunks( s );
        s.cache.clear();
        s.tileset_loads = tilecontext->tileset_loads();
    }

    ++s.frame;
    maybe_take_screenshot( s );
    sync_camera_mode( s.cam );
    sync_saved_heading( s );
    // Meshes differ between the modes (see mesh_options), so a switch rebuilds every chunk.
    if( s.cam.first_person() != s.chunks_first_person ) {
        release_all_chunks( s );
        s.chunks_first_person = s.cam.first_person();
    }
    if( !s.env_camera_applied ) {
        apply_env_camera( s.cam );
        s.env_camera_applied = true;
    }

    // The scene is rendered at a multiple of the window size and averaged down when composited,
    // which keeps distant textures from turning to grainy noise. CATA_R3D_SUPERSAMPLE=1 turns
    // it off (it costs fill rate), larger values go up to 3.
    static const int supersample = []() {
        const char *env = std::getenv( "CATA_R3D_SUPERSAMPLE" );
        return env ? std::clamp( std::atoi( env ), 1, 3 ) : 2;
    }
    ();
    const int target_w = std::max( 1, static_cast<int>( w ) ) * supersample;
    const int target_h = std::max( 1, static_cast<int>( h ) ) * supersample;
    if( !s.device.ensure_target( target_w, target_h ) ) {
        return false;
    }

    map &here = get_map();
    const tripoint_abs_ms origin = here.get_abs( center );
    const int view_z = origin.z();
    const bool multi_z = here.supports_zlevels() != 0;
    const int depth = multi_z ? get_option<int>( "R3D_VIEW_DEPTH" ) : 0;
    // Outdoors the storeys above are drawn too, so buildings stand at full height; under a roof
    // they are cut away to show the room.
    constexpr int max_levels_above = 5;
    const tripoint_bub_ms overhead = center + tripoint_rel_ms( 0, 0, 1 );
    const bool open_sky = !multi_z || !here.inbounds( overhead ) ||
                          s.cache.terrain( here.ter( overhead ), *tilecontext ).sh.kind == shape_kind::empty;
    const bool fp = s.cam.first_person();
    const tripoint_bub_ms player_pos = get_avatar().pos_bub();
    // In first person the eye stays on the player even when look mode moves the viewed tile, so
    // the levels cover both, and the storeys above are always drawn (they are the ceilings).
    const int low_z = fp ? std::min( view_z, player_pos.z() ) : view_z;
    const int high_z = fp ? std::max( view_z, player_pos.z() ) : view_z;
    const int z_hi = multi_z && ( open_sky || fp ) ?
                     std::min( high_z + max_levels_above, OVERMAP_HEIGHT ) : high_z;
    const int z_lo = std::max( low_z - depth, -OVERMAP_DEPTH );
    const tripoint_abs_sm base = here.get_abs_sub();

    // Only submaps inside the reality bubble: looking anything else up could load it from disk.
    const submap_lookup lookup = [ &base, view_z, multi_z ]( const tripoint_abs_sm & p ) {
        return loaded_submap_at( base, view_z, multi_z, p );
    };

    const int vision_floor = std::max( std::min( view_z, get_avatar().posz() - fov_3d_z_range ),
                                       -OVERMAP_DEPTH );
    const light_sampler sampler{ here, here.get_visibility_variables_cache(), get_avatar(),
                                 vision_floor, view_z };
    const uint64_t frame_light_key = light_key( here, origin, z_lo, z_hi );

    // The scene is drawn in a frame whose origin is the viewed tile, which keeps floats small.
    s.cam.z_scale = static_cast<float>( get_option<int>( "R3D_LEVEL_HEIGHT" ) ) / 100.0f;
    if( fp ) {
        float ease_x = 0.0f;
        float ease_y = 0.0f;
        float ease_z = 0.0f;
        const float stand = surface_height( s, here, player_pos );
        update_fp_ease( s, get_map().get_abs( player_pos ), stand, ease_x, ease_y, ease_z );
        // The camera stands on the player's tile, at the height of what they stand on.
        s.cam.target = tile_to_world( {
            static_cast<float>( player_pos.x() - center.x() ) + 0.5f + ease_x,
            static_cast<float>( player_pos.y() - center.y() ) + 0.5f + ease_y,
            static_cast<float>( player_pos.z() - center.z() ) + stand + ease_z
        } );
        // Looking at another level (look mode's < and >) tilts the view towards it, 45 degrees
        // per level at most one level's worth; the eye stays where the player stands.
        constexpr float level_tilt = 0.7854f;
        s.cam.tilt_pitch = static_cast<float>( std::clamp( view_z - player_pos.z(), -1, 1 ) ) *
                           level_tilt;
        s.cam.fovx = static_cast<float>( get_option<int>( "R3D_FP_FOV" ) ) * 0.017453293f;
        s.cam.eye_height = static_cast<float>( get_option<int>( "R3D_FP_EYE_HEIGHT" ) ) / 100.0f;
    } else {
        s.fp_prev_valid = false;
        s.fp_easing = false;
        s.cam.tween_yaw = 0.0f;
        s.cam.tilt_pitch = 0.0f;
        s.cam.target = tile_to_world( { 0.5f, 0.5f, 0.5f } ) + s.pan;
    }
    const float aspect = static_cast<float>( target_w ) / static_cast<float>( target_h );
    const mat4 view_proj = s.cam.view_projection( aspect );
    const frustum fr = s.cam.view_frustum( aspect );

    if( !s.device.begin_frame() ) {
        return false;
    }

    struct visible_chunk {
        const chunk_gpu *chunk;
        float offset[3];
    };
    std::vector<visible_chunk> visible;
    std::vector<entity_view::standing> upright_list;
    int builds_left = max_chunk_builds_per_frame;
    for( int z = z_lo; z <= z_hi; ++z ) {
        for( int gy = 0; gy < MAPSIZE; ++gy ) {
            for( int gx = 0; gx < MAPSIZE; ++gx ) {
                const tripoint_abs_sm pos( base.x() + gx, base.y() + gy, z );
                const tripoint_abs_ms chunk_origin = project_to<coords::ms>( pos );
                const float ox = static_cast<float>( chunk_origin.x() - origin.x() );
                const float oy = -static_cast<float>( chunk_origin.y() - origin.y() );
                const float oz = static_cast<float>( z - view_z );
                const aabb box{ { ox, oy - SEEY, oz }, { ox + SEEX, oy, oz + chunk_max_height } };
                if( !fr.intersects( box ) ) {
                    continue;
                }
                const uint64_t stamp = chunk_stamp( pos, lookup );
                if( stamp == 0 ) {
                    continue; // not loaded
                }
                chunk_gpu &c = s.chunks[chunk_key{ pos.x(), pos.y(), pos.z() }];
                c.last_used = s.frame;
                if( ( !c.built || c.stamp != stamp ) && builds_left > 0 ) {
                    --builds_left;
                    rebuild_chunk( s, c, pos, stamp, lookup );
                }
                if( c.built && !c.batches.empty() ) {
                    if( c.light_key != frame_light_key ) {
                        update_chunk_light( s, c, pos, origin, center, sampler, frame_light_key );
                    }
                    visible.push_back( { &c, { ox, oy, oz } } );
                }
                if( c.built ) {
                    const tripoint_bub_ms chunk_base( center.x() + ( chunk_origin.x() - origin.x() ),
                                                      center.y() + ( chunk_origin.y() - origin.y() ), z );
                    for( const upright_sprite &u : c.uprights ) {
                        upright_list.push_back( { chunk_base + tripoint_rel_ms( static_cast<int>( u.lx ), static_cast<int>( u.ly ), 0 ), u.z_base,
                                                  u.info } );
                    }
                }
            }
        }
    }

    // Creatures and items change every frame, so their geometry is rebuilt and re-uploaded.
    entity_view entities_view{ *tilecontext, s.cache, s.cam, center, z_lo, z_hi,
                               [&sampler]( const tripoint_bub_ms & p )
    {
        return sampler.color_at( p );
    }, {}, std::move( upright_list ), [&s, &here]( const tripoint_bub_ms & p )
    {
        return surface_height( s, here, p );
    } };
    entities_view.upright_billboards = fp;
    // The game queues its cursors and highlights for cata_tiles::draw, which is skipped here.
    {
        const cata_tiles::frame_markers queued = tilecontext->get_frame_markers();
        const auto add_markers = [ &s, &here, &entities_view ]( const std::vector<tripoint_bub_ms> &where,
                                 const std::string & id,
        bool box, bool large ) {
            append_frame_markers( s, here, entities_view, where, id, box, large );
        };
        add_markers( queued.line, "animation_line", false, false );
        if( queued.line_end && !queued.line_end_id.empty() ) {
            add_markers( { *queued.line_end }, queued.line_end_id, false, true );
        }
        add_markers( queued.highlights, "highlight", false, true );
        add_markers( queued.cursors, "cursor", true, true );
        // Sounds heard this turn, which cata_tiles::draw_footsteps_frame would draw: posts on
        // their tile with normal depth, so walls and creatures in front hide them. The 2D view
        // draws them over everything; here the compass strip points at the hidden ones instead
        // (out of the picture, or behind something solid).
        s.heard_ticks.clear();
        if( fp ) {
            const int player_z = player_pos.z();
            const bool occlude_unseen = get_option<bool>( "R3D_FP_OCCLUDE_UNSEEN" );
            // What hides a post: terrain and furniture as drawn. Unseen places only when they
            // are drawn solid (R3D_FP_OCCLUDE_UNSEEN); creatures never fill a whole tile.
            const column_height_fn solid = [ &center, z_lo, z_hi, &here, occlude_unseen, &sampler, &s ]( int dx,
            int dy, int dz ) {
                return occluding_column_height( s, here, sampler, center, z_lo, z_hi, occlude_unseen, dx, dy, dz );
            };
            const vec3 eye_world = s.cam.eye();
            const vec3 eye_tile = world_to_tile( { eye_world.x, eye_world.y,
                                                   eye_world.z / s.cam.z_scale } );
            const float view_yaw = s.cam.fp_yaw();
            for( const tripoint_bub_ms &p : sounds::get_footstep_markers() ) {
                const tripoint_rel_ms d = p - player_pos;
                const float rel = relative_bearing( static_cast<float>( d.x() ),
                                                    static_cast<float>( d.y() ), view_yaw );
                // The middle of the post (see r3d_entities.cpp), in the scene's tile space.
                const vec3 post{ static_cast<float>( p.x() - center.x() ) + 0.5f,
                                 static_cast<float>( p.y() - center.y() ) + 0.5f,
                                 static_cast<float>( p.z() - center.z() ) + column_height( s, here, p ) + 0.4f };
                if( std::abs( rel ) > s.cam.fp_fovx( aspect ) * 0.5f ||
                    !clear_line( eye_tile, post, solid ) ) {
                    s.heard_ticks.push_back( p );
                }
                const char *const id = p.z() > player_z ? "footstep_above" :
                                       p.z() < player_z ? "footstep_below" : "footstep";
                const tile_info *sprite = &s.cache.named( TILE_CATEGORY::NONE, id, *tilecontext );
                if( !sprite->main().valid ) {
                    sprite = &s.cache.named( TILE_CATEGORY::NONE, "footstep", *tilecontext );
                }
                if( !sprite->main().valid ) {
                    sprite = &s.cache.named( TILE_CATEGORY::NONE, "highlight", *tilecontext );
                }
                entities_view.markers.push_back( { p, column_height( s, here, p ), sprite, false, false,
                                                   true } );
            }
        }
    }
    const chunk_mesh entities = build_entity_mesh( entities_view );
    const bool draw_entities = !entities.empty() &&
                               upload_dynamic( s, s.dyn_positions, s.dyn_position_bytes, SDL_GPU_BUFFERUSAGE_VERTEX,
                                       entities.vertices.data(),
                                       static_cast<uint32_t>( entities.vertices.size() * sizeof( vertex ) ) ) &&
                               upload_dynamic( s, s.dyn_lights, s.dyn_light_bytes, SDL_GPU_BUFFERUSAGE_VERTEX,
                                       entities.light.data(),
                                       static_cast<uint32_t>( entities.light.size() * sizeof( uint32_t ) ) ) &&
                               upload_dynamic( s, s.dyn_indices, s.dyn_index_bytes, SDL_GPU_BUFFERUSAGE_INDEX,
                                       entities.indices.data(),
                                       static_cast<uint32_t>( entities.indices.size() * sizeof( uint32_t ) ) );

    // The background is what shows through open air. In first person that is the sky, which
    // follows the time of day (grey-blue by day, near black at night).
    float sky[3] = { 0.05f, 0.05f, 0.07f };
    if( fp ) {
        const float day = std::clamp( sun_moon_light_at( calendar::turn ) / default_daylight_level(),
                                      0.0f, 1.0f );
        constexpr float night_sky[3] = { 0.02f, 0.025f, 0.05f };
        constexpr float day_sky[3] = { 0.50f, 0.65f, 0.85f };
        for( int i = 0; i < 3; ++i ) {
            sky[i] = night_sky[i] + ( day_sky[i] - night_sky[i] ) * day;
        }
    }
    if( !s.device.begin_pass( sky[0], sky[1], sky[2] ) ) {
        s.device.abort_frame();
        // This frame's uploads may not have happened, so no chunk's buffers can be trusted.
        release_all_chunks( s );
        return false;
    }
    s.device.bind_block_pipeline();
    block_uniforms uniforms;
    uniforms.view_proj = view_proj;
    set_cutaway( uniforms, view_proj, aspect, s.cam, center );
    // First person: never-seen places hide what lies behind them (R3D_FP_OCCLUDE_UNSEEN).
    uniforms.flags[0] = fp && get_option<bool>( "R3D_FP_OCCLUDE_UNSEEN" ) ? 1.0f : 0.0f;
    for( const visible_chunk &vc : visible ) {
        uniforms.offset[0] = vc.offset[0];
        uniforms.offset[1] = vc.offset[1];
        uniforms.offset[2] = vc.offset[2];
        for( const mesh_batch &b : vc.chunk->batches ) {
            s.device.draw_block( vc.chunk->positions, vc.chunk->lights, vc.chunk->indices,
                                 b.first_index, b.index_count, b.sheet, uniforms );
        }
    }
    if( draw_entities ) {
        s.device.bind_entity_pipeline();
        uniforms.cut[3] = 0.0f; // creatures in front of the player stay
        // Unseen creatures and items are not drawn at all; unseen trees are left out.
        uniforms.flags[0] = 0.0f;
        uniforms.offset[0] = 0.0f;
        uniforms.offset[1] = 0.0f;
        uniforms.offset[2] = 0.0f;
        // Billboard tops get their depth pulled towards the camera (world direction: the
        // stretched view direction with its height shrunk back).
        const vec3 toward = normalize( s.cam.eye() - s.cam.stretched_target() );
        uniforms.pull[0] = toward.x;
        uniforms.pull[1] = toward.y;
        uniforms.pull[2] = toward.z / s.cam.z_scale;
        uniforms.pull[3] = fp ? 0.0f : max_billboard_pull; // upright sprites need no pull
        for( const mesh_batch &b : entities.batches ) {
            s.device.draw_block( s.dyn_positions, s.dyn_lights, s.dyn_indices, b.first_index, b.index_count,
                                 b.sheet, uniforms );
        }
    }
    SDL_Texture *const result = s.device.end_frame();
    if( !result ) {
        return false;
    }
    const SDL_FRect dst{ x, y, w, h };
    SDL_RenderTexture( renderer, result, nullptr, &dst );

    // Drop chunks that have not been needed for a while (the player moved on).
    for( auto it = s.chunks.begin(); it != s.chunks.end(); ) {
        if( s.frame - it->second.last_used > chunk_idle_frames ) {
            release_chunk( s, it->second );
            it = s.chunks.erase( it );
        } else {
            ++it;
        }
    }
    // Only now that the frame is on screen: on failure the flat view draws the markers instead.
    tilecontext->drop_frame_layers();
    s.shown = { true, x, y, w, h, s.cam, center, z_lo, z_hi };
    s.drawn = true;
    if( fp ) {
        draw_fp_hud( s, renderer, x, y, w, h, s.cam );
    }
    return true;
}

} // namespace r3d

#endif // defined(TILES)
