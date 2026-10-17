#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "cata_catch.h"
#include "r3d_camera.h"
#include "r3d_dda.h"
#include "r3d_hud_math.h"
#include "r3d_math.h"
#include "r3d_shapes.h"
#include "r3d_wall_rules.h"

namespace
{

bool approx( float a, float b, float eps = 1e-3f )
{
    return std::fabs( a - b ) <= eps;
}

} // namespace

TEST_CASE( "r3d_matrix_inverse_round_trips", "[r3d]" )
{
    r3d::camera cam;
    cam.target = { 3.0f, -7.0f, 2.0f };
    cam.yaw = 0.7f;
    const r3d::mat4 vp = cam.view_projection( 16.0f / 9.0f );
    r3d::mat4 inv;
    REQUIRE( r3d::invert( vp, inv ) );
    const r3d::mat4 id = vp * inv;
    for( int r = 0; r < 4; ++r ) {
        for( int c = 0; c < 4; ++c ) {
            CHECK( approx( id.at( r, c ), r == c ? 1.0f : 0.0f, 1e-2f ) );
        }
    }
}

TEST_CASE( "r3d_camera_looks_at_target", "[r3d]" )
{
    for( bool ortho : { false, true } ) {
        r3d::camera cam;
        cam.orthographic = ortho;
        cam.target = { 10.5f, -4.5f, 1.5f };
        cam.yaw = 1.1f;
        cam.pitch = 0.8f;
        cam.distance = 30.0f;
        const float aspect = 1.6f;

        // Target projects to the middle of the screen and in front of the camera.
        const r3d::vec3 ndc = r3d::transform_point( cam.view_projection( aspect ), cam.target );
        CHECK( approx( ndc.x, 0.0f ) );
        CHECK( approx( ndc.y, 0.0f ) );
        CHECK( ndc.z > 0.0f );
        CHECK( ndc.z < 1.0f );

        // The centre ray passes through the target.
        const r3d::ray centre = cam.screen_ray( 0.0f, 0.0f, aspect );
        const r3d::vec3 to_target = cam.target - centre.origin;
        const float along = r3d::dot( to_target, centre.dir );
        const r3d::vec3 closest = centre.origin + centre.dir * along;
        CHECK( r3d::length( closest - cam.target ) < 1e-2f );
    }
}

TEST_CASE( "r3d_camera_screen_ray_round_trips_projection", "[r3d]" )
{
    r3d::camera cam;
    cam.yaw = -0.4f;
    const float aspect = 1.78f;
    const r3d::mat4 vp = cam.view_projection( aspect );
    for( float nx : { -0.8f, 0.0f, 0.5f } ) {
        for( float ny : { -0.6f, 0.2f, 0.9f } ) {
            const r3d::ray r = cam.screen_ray( nx, ny, aspect );
            // Any point along the ray must project back to the same screen position.
            const r3d::vec3 p = r.origin + r.dir * 20.0f;
            const r3d::vec3 ndc = r3d::transform_point( vp, p );
            CHECK( approx( ndc.x, nx, 1e-2f ) );
            CHECK( approx( ndc.y, ny, 1e-2f ) );
        }
    }
}

TEST_CASE( "r3d_camera_orientation_matches_tile_space", "[r3d]" )
{
    // yaw 0: camera south of the target looking north, so north (tile -y) is up on screen and east
    // (tile +x) is to the right.
    r3d::camera cam;
    cam.yaw = 0.0f;
    cam.target = r3d::tile_to_world( { 0.5f, 0.5f, 0.0f } );
    const r3d::mat4 vp = cam.view_projection( 1.0f );
    const r3d::vec3 centre = r3d::transform_point( vp, cam.target );
    const r3d::vec3 east = r3d::transform_point( vp, r3d::tile_to_world( { 3.5f, 0.5f, 0.0f } ) );
    const r3d::vec3 north = r3d::transform_point( vp, r3d::tile_to_world( { 0.5f, -2.5f, 0.0f } ) );
    CHECK( east.x > centre.x );
    CHECK( north.y > centre.y );

    // A quarter turn clockwise (seen from above) puts the camera east of the target looking west:
    // west is now straight ahead (up on screen) and north is to the right.
    cam.yaw = 1.5707963f;
    const r3d::mat4 vp2 = cam.view_projection( 1.0f );
    const r3d::vec3 c2 = r3d::transform_point( vp2, cam.target );
    const r3d::vec3 west = r3d::transform_point( vp2, r3d::tile_to_world( { -2.5f, 0.5f, 0.0f } ) );
    const r3d::vec3 north2 = r3d::transform_point( vp2, r3d::tile_to_world( { 0.5f, -2.5f, 0.0f } ) );
    CHECK( west.y > c2.y );
    CHECK( north2.x > c2.x );
}

TEST_CASE( "r3d_camera_forward_eighths", "[r3d]" )
{
    // yaw 0 puts the camera south looking north; yaw grows clockwise seen from above, which
    // swings the camera east of the target, looking west.
    r3d::camera cam;
    cam.yaw = 0.0f;
    CHECK( cam.forward_eighths() == 0 );
    cam.yaw = 1.5707963f;
    CHECK( cam.forward_eighths() == 6 );
    cam.yaw = 3.1415927f;
    CHECK( cam.forward_eighths() == 4 );
    cam.yaw = -1.5707963f;
    CHECK( cam.forward_eighths() == 2 );
    cam.yaw = 0.7853982f;
    CHECK( cam.forward_eighths() == 7 );
    // Taller storeys or pitch do not change the heading.
    cam.z_scale = 1.6f;
    cam.pitch = 1.4f;
    CHECK( cam.forward_eighths() == 7 );
}

TEST_CASE( "r3d_camera_steps_clamp", "[r3d]" )
{
    r3d::camera cam;
    cam.pitch_steps( 1000 );
    CHECK( cam.pitch <= r3d::camera::max_pitch );
    cam.pitch_steps( -1000 );
    CHECK( cam.pitch >= r3d::camera::min_pitch );
    cam.zoom_steps( 1000 );
    CHECK( cam.distance >= r3d::camera::min_distance );
    cam.zoom_steps( -1000 );
    CHECK( cam.distance <= r3d::camera::max_distance );

    cam.yaw = 0.0f;
    cam.rotate_steps( 24 ); // 24 * 15 degrees = a full turn
    CHECK( ( approx( cam.yaw, 0.0f, 0.05f ) || approx( std::fabs( cam.yaw ), 6.2831853f, 0.05f ) ) );
}

TEST_CASE( "r3d_frustum_culls_boxes", "[r3d]" )
{
    r3d::camera cam;
    cam.target = { 0.0f, 0.0f, 0.0f };
    const r3d::frustum f = cam.view_frustum( 1.5f );
    CHECK( f.intersects( { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } } ) );
    CHECK_FALSE( f.intersects( { { 500.0f, 0.0f, 0.0f }, { 501.0f, 1.0f, 1.0f } } ) );
    CHECK_FALSE( f.intersects( { { 0.0f, 0.0f, 900.0f }, { 1.0f, 1.0f, 901.0f } } ) );
}

TEST_CASE( "r3d_raycast_hits_floor_tile_under_ray", "[r3d]" )
{
    // A flat floor of height 0.1 at z = 0 over x, y in [0, 20).
    const r3d::column_height_fn floor_world = []( int x, int y, int z ) {
        return z == 0 && x >= 0 && x < 20 && y >= 0 && y < 20 ? 0.1f : 0.0f;
    };
    // Straight down onto tile (7, 5).
    const r3d::voxel_hit down = r3d::raycast_columns( { 7.5f, 5.5f, 10.0f }, { 0.0f, 0.0f, -1.0f },
                                100.0f, floor_world );
    REQUIRE( down.hit );
    CHECK( down.x == 7 );
    CHECK( down.y == 5 );
    CHECK( down.z == 0 );
    CHECK( down.face == r3d::hit_face::top );
    CHECK( approx( down.t, 9.9f ) );

    // A ray pointing away misses.
    const r3d::voxel_hit up = r3d::raycast_columns( { 7.5f, 5.5f, 10.0f }, { 0.0f, 0.0f, 1.0f },
                              100.0f, floor_world );
    CHECK_FALSE( up.hit );

    // Out of range misses.
    const r3d::voxel_hit short_ray = r3d::raycast_columns( { 7.5f, 5.5f, 10.0f }, { 0.0f, 0.0f, -1.0f },
                                     5.0f, floor_world );
    CHECK_FALSE( short_ray.hit );
}

TEST_CASE( "r3d_raycast_wall_blocks_ground_behind_it", "[r3d]" )
{
    const r3d::column_height_fn world = []( int x, int y, int z ) {
        if( z != 0 || x < 0 || x >= 20 || y < 0 || y >= 20 ) {
            return 0.0f;
        }
        return x == 10 ? 1.0f : 0.1f; // a full-height wall along x = 10
    };
    // Looking east and gently down: the ray is still above the wall's top when it reaches x = 10,
    // so it must hit the wall's west face before it ever reaches the floor.
    const r3d::vec3 dir = r3d::normalize( { 1.0f, 0.0f, -0.05f } );
    const r3d::voxel_hit h = r3d::raycast_columns( { 2.5f, 8.5f, 0.9f }, dir, 100.0f, world );
    REQUIRE( h.hit );
    CHECK( h.x == 10 );
    CHECK( h.face == r3d::hit_face::west );
}

TEST_CASE( "r3d_pick_finds_tile_drawn_under_cursor", "[r3d]" )
{
    // Floor everywhere on level 0 of the scene frame and a wall at tile (2, -3).
    const r3d::column_height_fn world = []( int x, int y, int z ) {
        if( z != 0 ) {
            return 0.0f;
        }
        return x == 2 && y == -3 ? 1.0f : 0.1f;
    };
    for( float yaw : { 0.0f, 0.9f, 2.6f, 4.4f } ) {
        for( int variant = 0; variant < 3; ++variant ) {
            const bool ortho = variant == 1;
            r3d::camera cam;
            cam.target = r3d::tile_to_world( { 0.5f, 0.5f, 0.5f } ); // as r3d::draw sets it
            cam.yaw = yaw;
            cam.orthographic = ortho;
            cam.z_scale = variant == 2 ? 1.6f : 1.0f; // taller storeys must not change picks
            const float aspect = 1.5f;
            const r3d::mat4 vp = cam.view_projection( aspect );
            // The middle of the screen is the viewed tile.
            const r3d::voxel_hit centre = r3d::pick_cell( cam, 0.0f, 0.0f, aspect, 500.0f, world,
                                          r3d::floor_height );
            REQUIRE( centre.hit );
            CHECK( centre.x == 0 );
            CHECK( centre.y == 0 );
            CHECK( centre.z == 0 );
            // Where a floor tile's centre is drawn, that tile is picked (unless the wall covers it).
            for( int ty = -4; ty <= 4; ++ty ) {
                for( int tx = -4; tx <= 4; ++tx ) {
                    const r3d::vec3 top = { tx + 0.5f, ty + 0.5f, world( tx, ty, 0 ) };
                    const r3d::vec3 ndc = r3d::transform_point( vp, r3d::tile_to_world( top ) );
                    const r3d::voxel_hit h = r3d::pick_cell( cam, ndc.x, ndc.y, aspect, 500.0f, world,
                                             r3d::floor_height );
                    REQUIRE( h.hit );
                    const bool is_tile = h.x == tx && h.y == ty && h.z == 0;
                    const bool is_wall = h.x == 2 && h.y == -3;
                    CHECK( ( is_tile || is_wall ) );
                }
            }
        }
    }
}

TEST_CASE( "r3d_pick_falls_back_to_floor_plane", "[r3d]" )
{
    const r3d::column_height_fn nothing = []( int, int, int ) {
        return 0.0f;
    };
    r3d::camera cam;
    cam.target = r3d::tile_to_world( { 0.5f, 0.5f, 0.5f } );
    const r3d::voxel_hit h = r3d::pick_cell( cam, 0.0f, 0.0f, 1.0f, 500.0f, nothing,
                             r3d::floor_height );
    REQUIRE( h.hit );
    CHECK( h.x == 0 );
    CHECK( h.y == 0 );
    CHECK( h.z == 0 );
    CHECK( h.face == r3d::hit_face::top );
    // Above the horizon nothing can be picked.
    cam.pitch = r3d::camera::min_pitch;
    CHECK_FALSE( r3d::pick_cell( cam, 0.0f, 1.0f, 1.0f, 500.0f, nothing, r3d::floor_height ).hit );
}

TEST_CASE( "r3d_raycast_matches_reference", "[r3d]" )
{
    std::mt19937 rng( 12345 );
    std::uniform_int_distribution<int> kind( 0, 9 );
    std::uniform_real_distribution<float> unit( 0.0f, 1.0f );

    constexpr int size = 12;
    constexpr int layers = 5;
    for( int world_idx = 0; world_idx < 30; ++world_idx ) {
        std::vector<float> heights( size * size * layers, 0.0f );
        for( float &h : heights ) {
            const int k = kind( rng );
            h = k < 5 ? 0.0f : ( k < 7 ? 0.1f : ( k < 9 ? 0.5f : 1.0f ) );
        }
        const r3d::column_height_fn fn = [&]( int x, int y, int z ) {
            if( x < 0 || y < 0 || z < 0 || x >= size || y >= size || z >= layers ) {
                return 0.0f;
            }
            return heights[( z * size + y ) * size + x];
        };
        for( int ray_idx = 0; ray_idx < 200; ++ray_idx ) {
            const r3d::vec3 o{ unit( rng ) * size, unit( rng ) * size, unit( rng ) * layers + 0.5f };
            const r3d::vec3 d = r3d::normalize( { unit( rng ) - 0.5f, unit( rng ) - 0.5f, unit( rng ) - 0.7f } );
            if( r3d::length( d ) < 0.5f ) {
                continue;
            }
            const r3d::voxel_hit fast = r3d::raycast_columns( o, d, 60.0f, fn );
            const r3d::voxel_hit slow = r3d::raycast_columns_reference( o, d, 60.0f, fn,
                                        0, 0, 0, size - 1, size - 1, layers - 1 );
            INFO( "world " << world_idx << " ray " << ray_idx );
            REQUIRE( fast.hit == slow.hit );
            if( fast.hit ) {
                CHECK( fast.x == slow.x );
                CHECK( fast.y == slow.y );
                CHECK( fast.z == slow.z );
                CHECK( approx( fast.t, slow.t, 1e-3f ) );
            }
        }
    }
}

TEST_CASE( "r3d_terrain_shapes", "[r3d]" )
{
    r3d::tile_traits t;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::floor );

    t = {};
    t.no_floor = true;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::empty );

    t = {};
    t.wall = true;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::block );

    t = {};
    t.move_cost = 0; // closed door or window: impassable and not flat
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::block );

    t = {};
    t.move_cost = 0;
    t.flat = true;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::floor );

    t = {};
    t.tree = true;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::upright );

    t = {};
    t.shrub = true;
    const r3d::shape shrub = r3d::classify_terrain( t );
    CHECK( shrub.kind == r3d::shape_kind::upright );
    CHECK( shrub.height < 1.0f );

    // Windows, broken or not, and closed doors stand in the wall line as panes.
    t = {};
    t.window = true;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::pane );
    t.move_cost = 0;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::pane );
    t = {};
    t.door = true;
    t.move_cost = 0;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::pane );
    // An open door lies flat; a wall stays a wall.
    t.flat = true;
    t.move_cost = 2;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::floor );
    t = {};
    t.door = true;
    t.wall = true;
    t.move_cost = 0;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::block );
}

TEST_CASE( "r3d_furniture_box_layout", "[r3d]" )
{
    // A sprite filling its whole cell: a whole-tile box, lid above the front.
    const r3d::box_layout full = r3d::furniture_box_layout( 0.0f, 0.0f, 1.0f, 1.0f, 0.4f, false );
    CHECK( approx( full.x0, 0.0f ) );
    CHECK( approx( full.x1, 1.0f ) );
    CHECK( approx( full.y0, 0.0f ) );
    CHECK( approx( full.y1, 1.0f ) );
    CHECK( approx( full.lid_bottom, 0.6f ) );
    CHECK( approx( full.side_top, 0.6f ) );
    CHECK( approx( full.side_bottom, 1.0f ) );

    // A narrow bench in the middle of its cell: the box is only as wide as the planks, and its
    // sides take the lid's front edge, so they line up with the top.
    const r3d::box_layout bench = r3d::furniture_box_layout( 0.3f, 0.2f, 0.7f, 0.9f, 0.4f, true );
    CHECK( approx( bench.x0, 0.3f ) );
    CHECK( approx( bench.x1, 0.7f ) );
    CHECK( approx( bench.col0, bench.x0 ) );
    CHECK( approx( bench.col1, bench.x1 ) );
    // Boxes stand against the back (north) of their tile, where a wall would be.
    CHECK( approx( bench.y0, 0.0f ) );
    CHECK( bench.y1 >= 0.5f );
    CHECK( bench.y1 <= 1.0f );
    // A tall piece whose art starts a little below the top still touches the back.
    const r3d::box_layout locker = r3d::furniture_box_layout( 0.0f, 0.06f, 1.0f, 1.0f, 0.78f, false );
    CHECK( approx( locker.y0, 0.0f ) );
    CHECK( approx( bench.side_bottom, bench.lid_bottom ) );
    CHECK( bench.side_top < bench.side_bottom );

    // No opaque pixels known: fall back to the whole sprite.
    const r3d::box_layout none = r3d::furniture_box_layout( 0.0f, 0.0f, 0.0f, 0.0f, 0.4f, false );
    CHECK( approx( none.x1, 1.0f ) );
    CHECK( approx( none.y1, 1.0f ) );
}

TEST_CASE( "r3d_furniture_shapes", "[r3d]" )
{
    // Small walkable furniture (chairs) stands up as a sprite.
    r3d::tile_traits t;
    const r3d::shape ordinary = r3d::classify_furniture( t );
    CHECK( ordinary.kind == r3d::shape_kind::upright );
    CHECK( ordinary.height > r3d::floor_height );

    t = {};
    t.tiny = true;
    const r3d::shape tiny = r3d::classify_furniture( t );
    CHECK( tiny.kind == r3d::shape_kind::floor );
    CHECK( tiny.height < ordinary.height );

    // Impassable furniture (lockers, fridges) is a tall box.
    t = {};
    t.move_cost = 0;
    const r3d::shape locker = r3d::classify_furniture( t );
    CHECK( locker.kind == r3d::shape_kind::prism );
    CHECK( locker.height > ordinary.height );

    // Benches and counters are low boxes, lower than lockers.
    t = {};
    t.low = true;
    const r3d::shape bench = r3d::classify_furniture( t );
    CHECK( bench.kind == r3d::shape_kind::prism );
    CHECK( bench.height < locker.height );
    // Benches and tables are a thin top on legs, cabinets are solid.
    CHECK( bench.slab > 0.0f );
    CHECK( bench.slab < bench.height );
    CHECK( locker.slab == 0.0f );
    t = {};
    t.flat_surface = true;
    CHECK( r3d::classify_furniture( t ).kind == r3d::shape_kind::prism );
    t.container = true;
    CHECK( r3d::classify_furniture( t ).slab == 0.0f );

    t = {};
    t.flat = true;
    const r3d::shape rug = r3d::classify_furniture( t );
    CHECK( rug.kind == r3d::shape_kind::floor );
    CHECK( rug.height < ordinary.height );
}

TEST_CASE( "r3d_face_visibility", "[r3d]" )
{
    const r3d::shape block{ r3d::shape_kind::block, 1.0f };
    const r3d::shape floor{ r3d::shape_kind::floor, r3d::floor_height };
    const r3d::shape air{};

    // A lone block shows everything except its bottom.
    r3d::neighbourhood open;
    CHECK( r3d::visible_faces( block, open ) ==
           ( r3d::face_top | r3d::face_north | r3d::face_south | r3d::face_west | r3d::face_east ) );

    // Block with another block to the east and above: the east side is hidden, but the top
    // stays, since the level above may be cut away.
    r3d::neighbourhood packed;
    packed.east = block;
    packed.up = block;
    const uint8_t faces = r3d::visible_faces( block, packed );
    CHECK( ( faces & r3d::face_east ) == 0 );
    CHECK( ( faces & r3d::face_top ) != 0 );
    CHECK( ( faces & r3d::face_west ) != 0 );

    // A block beside a floor still shows that side, because the floor is lower.
    r3d::neighbourhood beside_floor;
    beside_floor.south = floor;
    CHECK( ( r3d::visible_faces( block, beside_floor ) & r3d::face_south ) != 0 );

    // A floor only ever has a top face, even under a block.
    CHECK( r3d::visible_faces( floor, open ) == r3d::face_top );
    r3d::neighbourhood roofed;
    roofed.up = block;
    CHECK( r3d::visible_faces( floor, roofed ) == r3d::face_top );

    // Air, upright sprites and panes emit nothing through this path.
    CHECK( r3d::visible_faces( air, open ) == 0 );
    CHECK( r3d::visible_faces( { r3d::shape_kind::upright, 1.4f }, open ) == 0 );
    CHECK( r3d::visible_faces( { r3d::shape_kind::pane, 1.0f }, open ) == 0 );

    // A tall prism next to a short one shows its side; the short one's side is hidden.
    const r3d::shape tall{ r3d::shape_kind::prism, 0.9f };
    const r3d::shape low{ r3d::shape_kind::prism, 0.3f };
    r3d::neighbourhood tall_next_to_low;
    tall_next_to_low.east = low;
    CHECK( ( r3d::visible_faces( tall, tall_next_to_low ) & r3d::face_east ) != 0 );
    r3d::neighbourhood low_next_to_tall;
    low_next_to_tall.east = tall;
    CHECK( ( r3d::visible_faces( low, low_next_to_tall ) & r3d::face_east ) == 0 );
}

TEST_CASE( "r3d_wall_texture_matching", "[r3d]" )
{
    r3d::wall_texture_rules rules;
    rules.by_id = { { "t_wall", "plaster" }, { "t_wall_log_half", "logs" } };
    rules.by_prefix = { { "t_wall_", "plaster" }, { "t_wall_log", "logs" }, { "t_brick_wall", "brick" } };

    // Exact ids first, along the looks_like chain.
    CHECK( r3d::match_wall_texture( rules, { "t_wall" } ) == "plaster" );
    CHECK( r3d::match_wall_texture( rules, { "t_wall_log_half" } ) == "logs" );
    CHECK( r3d::match_wall_texture( rules, { "t_mod_wall", "t_wall" } ) == "plaster" );
    // Then the longest prefix.
    CHECK( r3d::match_wall_texture( rules, { "t_wall_log_widened" } ) == "logs" );
    CHECK( r3d::match_wall_texture( rules, { "t_wall_b" } ) == "plaster" );
    CHECK( r3d::match_wall_texture( rules, { "t_brick_wall_halfway" } ) == "brick" );
    // Nothing matches: the fallback, empty by default (keep the tileset sprite).
    CHECK( r3d::match_wall_texture( rules, { "t_vat" } ).empty() );
    rules.fallback = "concrete";
    CHECK( r3d::match_wall_texture( rules, { "t_vat" } ) == "concrete" );
}

TEST_CASE( "r3d_wall_atlas_cells", "[r3d]" )
{
    // 4 x 3 cells of 32 pixels.
    const r3d::uv_rect first = r3d::atlas_cell( 0, 32, 128, 96 );
    REQUIRE( first.valid );
    CHECK( approx( first.u0, 0.5f / 128.0f ) );
    CHECK( approx( first.v1, 31.5f / 96.0f ) );
    const r3d::uv_rect fifth = r3d::atlas_cell( 5, 32, 128, 96 );
    REQUIRE( fifth.valid );
    CHECK( approx( fifth.u0, 32.5f / 128.0f ) );
    CHECK( approx( fifth.v0, 32.5f / 96.0f ) );
    CHECK_FALSE( r3d::atlas_cell( 12, 32, 128, 96 ).valid );
    CHECK_FALSE( r3d::atlas_cell( -1, 32, 128, 96 ).valid );
}

TEST_CASE( "r3d_sky_direction_follows_calendar_convention", "[r3d]" )
{
    // Azimuth 0 is south: the sun stands to the south (world -y), at its altitude.
    const r3d::vec3 noon = r3d::sky_direction( 0.0f, 0.5f );
    CHECK( approx( noon.x, 0.0f ) );
    CHECK( noon.y < 0.0f );
    CHECK( approx( noon.z, std::sin( 0.5f ) ) );
    // Positive azimuth turns towards the west (world -x).
    const r3d::vec3 west = r3d::sky_direction( 1.5707963f, 0.0f );
    CHECK( approx( west.x, -1.0f ) );
    CHECK( approx( west.y, 0.0f ) );
    CHECK( approx( r3d::length( r3d::sky_direction( 0.3f, 0.7f ) ), 1.0f ) );
}

TEST_CASE( "r3d_sky_color_brightens_by_day_and_darkens_with_cloud", "[r3d]" )
{
    const r3d::rgb night = r3d::sky_color( 0.0f, 0.0f, false );
    const r3d::rgb day = r3d::sky_color( 1.0f, 0.0f, false );
    const r3d::rgb overcast = r3d::sky_color( 1.0f, 1.0f, false );
    CHECK( day.b > night.b );
    CHECK( day.r + day.g + day.b > overcast.r + overcast.g + overcast.b );
    // Overcast is grey: its channels are equal.
    CHECK( approx( overcast.r, overcast.b ) );
    CHECK( r3d::sky_color( 0.0f, 0.0f, true ).r > day.r );
}

TEST_CASE( "r3d_wind_blows_away_from_its_direction", "[r3d]" )
{
    // A north wind (from 0 degrees) blows south: world -y.
    const r3d::vec3 north = r3d::wind_velocity( 0, 5.0f );
    CHECK( approx( north.x, 0.0f ) );
    CHECK( approx( north.y, -5.0f ) );
    // An east wind blows west.
    const r3d::vec3 east = r3d::wind_velocity( 90, 2.0f );
    CHECK( approx( east.x, -2.0f ) );
    CHECK( approx( east.y, 0.0f, 1e-3f ) );
}

TEST_CASE( "r3d_particles_drift_with_the_wind_and_stay_in_their_box", "[r3d]" )
{
    r3d::particle_field f;
    f.half_extent = 10.0f;
    f.height = 4.0f;
    f.fall_speed = 3.0f;
    f.wind = r3d::wind_velocity( 270, 2.0f ); // from the west: drifts east
    double sum_dx = 0.0;
    double sum_dz = 0.0;
    for( uint32_t i = 0; i < 200; ++i ) {
        for( float t : { 0.0f, 0.37f, 5.0f, 123.4f } ) {
            const r3d::vec3 p = r3d::particle_position( i, t, f, { 3.5f, -2.0f, 0.0f } );
            CHECK( p.x >= -10.0f );
            CHECK( p.x < 10.0f + 1e-3f );
            CHECK( p.y >= -10.0f );
            CHECK( p.y < 10.0f + 1e-3f );
            CHECK( p.z >= 0.0f );
            CHECK( p.z < 4.0f + 1e-3f );
        }
        // Over a short step (no wrapping for most), particles move east and down.
        const r3d::vec3 a = r3d::particle_position( i, 1.0f, f );
        const r3d::vec3 b = r3d::particle_position( i, 1.05f, f );
        if( std::fabs( b.x - a.x ) < 1.0f && std::fabs( b.z - a.z ) < 1.0f ) {
            sum_dx += b.x - a.x;
            sum_dz += b.z - a.z;
        }
    }
    CHECK( sum_dx > 0.0 );
    CHECK( sum_dz < 0.0 );
    // The same particle at the same time is in the same place: the picture does not flicker.
    CHECK( approx( r3d::particle_position( 7, 2.0f, f ).x, r3d::particle_position( 7, 2.0f, f ).x ) );
}

TEST_CASE( "r3d_fog_grows_with_sight_penalty_and_distance", "[r3d]" )
{
    CHECK( r3d::fog_density( 1.0f ) == 0.0f );
    CHECK( r3d::fog_density( 1.7f ) > r3d::fog_density( 1.1f ) );
    const float d = r3d::fog_density( 1.7f );
    CHECK( r3d::fog_amount( 0.0f, d ) == 0.0f );
    CHECK( r3d::fog_amount( 10.0f, d ) < r3d::fog_amount( 40.0f, d ) );
    CHECK( r3d::fog_amount( 200.0f, d ) > 0.99f );
}

TEST_CASE( "r3d_bearings_match_the_camera", "[r3d]" )
{
    CHECK( approx( r3d::map_bearing( 0.0f, -1.0f ), 0.0f ) );          // north
    CHECK( approx( r3d::map_bearing( 1.0f, 0.0f ), 1.5707963f ) );     // east
    CHECK( approx( r3d::map_bearing( 0.0f, 1.0f ), 3.1415927f ) );     // south
    CHECK( approx( r3d::map_bearing( -1.0f, 0.0f ), 4.712389f ) );     // west
    for( int step = -12; step <= 12; ++step ) {
        r3d::camera cam;
        cam.yaw = 0.2617994f * static_cast<float>( step );
        const float fwd = r3d::camera_forward_bearing( cam.yaw );
        const float eighths = static_cast<float>( cam.forward_eighths() ) * 0.7853982f;
        CHECK( std::fabs( r3d::bearing_delta( eighths, fwd ) ) <= 0.3927f + 1e-3f );
    }
    CHECK( approx( r3d::bearing_delta( 6.1f, 0.1f ), 0.1f + 6.2831853f - 6.1f ) );
    CHECK( approx( r3d::bearing_delta( 0.1f, 6.1f ), -( 0.1f + 6.2831853f - 6.1f ) ) );
}

TEST_CASE( "r3d_edge_marks_sit_on_the_border_and_point_out", "[r3d]" )
{
    // On screen: no mark.
    CHECK( r3d::place_edge_mark( 0.2f, -0.3f, 1.0f, 0.1f, 0.1f ).on_screen );
    // Far to the right: on the right border, pointing right.
    const r3d::edge_mark right = r3d::place_edge_mark( 5.0f, 0.5f, 1.0f, 0.1f, 0.1f );
    CHECK_FALSE( right.on_screen );
    CHECK( approx( right.x, 0.9f ) );
    CHECK( approx( right.y, 0.09f ) );
    CHECK( std::fabs( right.angle ) < 0.2f );
    // Up and left: on the top border, pointing up-left.
    const r3d::edge_mark up = r3d::place_edge_mark( -1.0f, 4.0f, 1.0f, 0.1f, 0.1f );
    CHECK( approx( up.y, 0.9f ) );
    CHECK( up.x < 0.0f );
    CHECK( up.angle > 1.5707963f );
    // Behind the camera, the projection is mirrored back.
    const r3d::edge_mark behind = r3d::place_edge_mark( 0.5f, 0.0f, -1.0f, 0.1f, 0.1f );
    CHECK_FALSE( behind.on_screen );
    CHECK( behind.x < 0.0f );
    const r3d::edge_mark straight_back = r3d::place_edge_mark( 0.0f, 0.0f, -1.0f, 0.1f, 0.1f );
    CHECK( approx( straight_back.y, -0.9f ) );
}
