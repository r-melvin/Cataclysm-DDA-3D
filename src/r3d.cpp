#if defined(TILES)

#include "r3d.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_tiles.h"
#include "coordinates.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapbuffer.h"
#include "options.h"
#include "r3d_camera.h"
#include "r3d_entities.h"
#include "r3d_gpu.h"
#include "r3d_mesh.h"
#include "sdltiles.h"
#include "submap.h"

namespace r3d
{

namespace
{

constexpr int max_chunk_builds_per_frame = 8;
constexpr uint64_t chunk_idle_frames = 600;
// Tallest geometry in a chunk: trees are 1.4 above a 0.1 floor.
constexpr float chunk_max_height = 1.6f;

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
    std::vector<light_ref> refs; // where each vertex samples its light
    std::vector<uint32_t> light; // current RGBA8 light per vertex
    uint64_t light_key = 0;      // light_key() the stream was last computed for
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
    SDL_Texture *probe_sheet = nullptr;
    bool env_camera_applied = false;
    bool screenshot_taken = false;
    // Per-frame buffers for creatures and items, grown as needed and re-uploaded every frame.
    SDL_GPUBuffer *dyn_positions = nullptr;
    SDL_GPUBuffer *dyn_lights = nullptr;
    SDL_GPUBuffer *dyn_indices = nullptr;
    uint32_t dyn_position_bytes = 0;
    uint32_t dyn_light_bytes = 0;
    uint32_t dyn_index_bytes = 0;
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
bool upload_dynamic( state &s, SDL_GPUBuffer *&buffer, uint32_t &capacity, SDL_GPUBufferUsageFlags usage,
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
    chunk_mesh mesh = build_chunk_mesh( pos, lookup, s.cache, *tilecontext );
    release_chunk( s, c );
    c.stamp = stamp;
    c.built = true;
    if( mesh.empty() ) {
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
        !s.device.upload( c.lights, mesh.light.data(), light_bytes ) ||
        !s.device.upload( c.indices, mesh.indices.data(), index_bytes ) ) {
        release_chunk( s, c );
        return false;
    }
    c.batches = std::move( mesh.batches );
    c.refs = std::move( mesh.light_refs );
    c.light = std::move( mesh.light );
    return true;
}

// Turns the game's own visibility knowledge into a light colour per tile: lit tiles are shown
// normally, unseen but remembered ones dim and bluish, everything else black.
struct light_sampler {
    const map &here;
    const visibility_variables &vvc;
    const avatar &you;

    uint32_t color_at( const tripoint_bub_ms &p ) const {
        if( !here.inbounds( p ) ) {
            return pack_light( 0, 0, 0 );
        }
        // Debug aid: CATA_R3D_FULLBRIGHT=1 shows all loaded geometry regardless of visibility.
        static const bool fullbright = std::getenv( "CATA_R3D_FULLBRIGHT" ) != nullptr;
        if( fullbright ) {
            return pack_light( 255, 255, 255 );
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
                case lit_level::BRIGHT_ONLY:
                    r = g = b = 0.9f;
                    break;
                case lit_level::LOW:
                    r = g = b = 0.75f;
                    break;
                case lit_level::DARK:
                    r = g = b = 0.6f;
                    break;
                default:
                    break;
            }
        } else if( you.has_memory_at( here.get_abs( p ) ) ) {
            r = 0.25f;
            g = 0.30f;
            b = 0.42f;
        }
        const auto to_byte = []( float v ) {
            return static_cast<uint8_t>( std::clamp( v, 0.0f, 1.0f ) * 255.0f + 0.5f );
        };
        return pack_light( to_byte( r ), to_byte( g ), to_byte( b ) );
    }
};

uint64_t light_key( const tripoint_abs_ms &origin )
{
    uint64_t key = static_cast<uint64_t>( to_turn<int>( calendar::turn ) );
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
        const uint32_t tile = grid[std::clamp( static_cast<int>( r.dz ), 0, 1 )][static_cast<size_t>
                              ( gy * border + gx )];
        const uint32_t shade = r.shade;
        c.light[i] = pack_light( static_cast<uint8_t>( ( ( tile >> 0 ) & 255u ) * shade / 255u ),
                                 static_cast<uint8_t>( ( ( tile >> 8 ) & 255u ) * shade / 255u ),
                                 static_cast<uint8_t>( ( ( tile >> 16 ) & 255u ) * shade / 255u ) );
    }
    s.device.upload( c.lights, c.light.data(),
                     static_cast<uint32_t>( c.light.size() * sizeof( uint32_t ) ), true );
}

void apply_env_camera( camera &cam )
{
    // CATA_R3D_CAMERA="yaw_deg,pitch_deg,distance" gives repeatable views for screenshots.
    const char *spec = std::getenv( "CATA_R3D_CAMERA" );
    if( !spec ) {
        return;
    }
    float yaw = 0.0f;
    float pitch = 0.0f;
    float dist = 0.0f;
    if( std::sscanf( spec, "%f,%f,%f", &yaw, &pitch, &dist ) == 3 ) {
        constexpr float deg = 0.017453293f;
        cam.yaw = yaw * deg;
        cam.pitch = std::clamp( pitch * deg, camera::min_pitch, camera::max_pitch );
        cam.distance = std::clamp( dist, camera::min_distance, camera::max_distance );
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

} // namespace

bool active()
{
    return st().drawn && get_option<bool>( "RENDERER_3D" );
}

void toggle()
{
    get_options().get_option( "RENDERER_3D" ).setNext();
    get_options().save();
}

void rotate( int dir )
{
    st().cam.rotate_steps( dir );
}

void pitch( int dir )
{
    st().cam.pitch_steps( dir );
}

void zoom( int dir )
{
    st().cam.zoom_steps( dir );
}

void reset()
{
    state &s = st();
    s.cam = camera();
    s.env_camera_applied = false;
}

void shutdown()
{
    state &s = st();
    release_all_chunks( s );
    release_dynamic( s );
    s.cache.clear();
    s.device.shutdown();
    s.epoch_valid = false;
    s.drawn = false;
}

bool draw( SDL_Renderer *renderer, float x, float y, float w, float h,
           const tripoint_bub_ms &center, unsigned render_epoch )
{
    state &s = st();
    s.drawn = false;
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

    // A tileset reload swaps the sheet textures without necessarily bumping the epoch.
    {
        const std::optional<texture_draw_data> probe = tilecontext->get_sprite_draw_data( "t_grass",
                TILE_CATEGORY::TERRAIN, tripoint_bub_ms( 0, 0, 0 ) );
        SDL_Texture *const sheet = probe ? probe->texture : nullptr;
        if( sheet != s.probe_sheet ) {
            release_all_chunks( s );
            s.cache.clear();
            s.probe_sheet = sheet;
        }
    }

    ++s.frame;
    maybe_take_screenshot( s );
    if( !s.env_camera_applied ) {
        apply_env_camera( s.cam );
        s.env_camera_applied = true;
    }

    const int target_w = std::max( 1, static_cast<int>( w ) );
    const int target_h = std::max( 1, static_cast<int>( h ) );
    if( !s.device.ensure_target( target_w, target_h ) ) {
        return false;
    }

    map &here = get_map();
    const tripoint_abs_ms origin = here.get_abs( center );
    const int view_z = origin.z();
    const bool multi_z = here.supports_zlevels() != 0;
    const int depth = multi_z ? get_option<int>( "R3D_VIEW_DEPTH" ) : 0;
    const int z_hi = view_z;
    const int z_lo = std::max( view_z - depth, -OVERMAP_DEPTH );
    const tripoint_abs_sm base = here.get_abs_sub();

    // Only submaps inside the reality bubble: looking anything else up could load it from disk.
    const submap_lookup lookup = [&]( const tripoint_abs_sm & p ) -> const submap * {
        const int dx = p.x() - base.x();
        const int dy = p.y() - base.y();
        if( dx < 0 || dy < 0 || dx >= MAPSIZE || dy >= MAPSIZE ||
            p.z() < -OVERMAP_DEPTH || p.z() > OVERMAP_HEIGHT )
        {
            return nullptr;
        }
        if( p.z() != view_z && !multi_z )
        {
            return nullptr;
        }
        return MAPBUFFER.lookup_submap( p );
    };

    const light_sampler sampler{ here, here.get_visibility_variables_cache(), get_avatar() };
    const uint64_t frame_light_key = light_key( origin );

    // The scene is drawn in a frame whose origin is the viewed tile, which keeps floats small.
    s.cam.target = tile_to_world( { 0.5f, 0.5f, 0.5f } );
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
            }
        }
    }

    // Creatures and items change every frame, so their geometry is rebuilt and re-uploaded.
    const entity_view entities_view{ *tilecontext, s.cache, s.cam, center, z_lo, z_hi,
                                     [&sampler]( const tripoint_bub_ms & p ) {
        return sampler.color_at( p );
    } };
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

    if( !s.device.begin_pass( 0.05f, 0.05f, 0.07f ) ) {
        s.device.abort_frame();
        return false;
    }
    s.device.bind_block_pipeline();
    block_uniforms uniforms;
    uniforms.view_proj = view_proj;
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
        uniforms.offset[0] = 0.0f;
        uniforms.offset[1] = 0.0f;
        uniforms.offset[2] = 0.0f;
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
    s.drawn = true;
    return true;
}

} // namespace r3d

#endif // defined(TILES)
