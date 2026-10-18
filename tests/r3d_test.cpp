#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "cata_catch.h"
#include "r3d_camera.h"
#include "r3d_dda.h"
#include "r3d_hud.h"
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
    for( bool ortho : {
             false, true
         } ) {
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
    for( float nx : {
             -0.8f, 0.0f, 0.5f
             } ) {
        for( float ny : {
                 -0.6f, 0.2f, 0.9f
                 } ) {
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
    for( float yaw : {
             0.0f, 0.9f, 2.6f, 4.4f
         } ) {
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
        const r3d::column_height_fn fn = [ &heights ]( int x, int y, int z ) {
            if( x < 0 || y < 0 || z < 0 || x >= size || y >= size || z >= layers ) {
                return 0.0f;
            }
            return heights[( z * size + y ) * size + x];
        };
        for( int ray_idx = 0; ray_idx < 200; ++ray_idx ) {
            const r3d::vec3 o{ unit( rng ) *size, unit( rng ) *size, unit( rng ) *layers + 0.5f };
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

    // A floor only ever has a top face, even under a block, plus a ceiling there (drawn by the
    // first-person view only).
    CHECK( r3d::visible_faces( floor, open ) == r3d::face_top );
    r3d::neighbourhood roofed;
    roofed.up = block;
    CHECK( r3d::visible_faces( floor, roofed ) == ( r3d::face_top | r3d::face_ceiling ) );

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

namespace
{

// A first-person camera standing on the middle of tile (0, 0) at level 0 with a storey 1.8 high.
r3d::camera first_person_camera( int heading )
{
    r3d::camera cam;
    cam.mode = r3d::camera_mode::first_person;
    cam.z_scale = 1.8f;
    cam.heading = heading;
    cam.target = r3d::tile_to_world( { 0.5f, 0.5f, 0.1f } );
    return cam;
}

} // namespace

TEST_CASE( "r3d_first_person_eye_stands_on_the_target", "[r3d]" )
{
    const r3d::camera cam = first_person_camera( 0 );
    const r3d::vec3 eye = cam.eye();
    CHECK( approx( eye.x, 0.5f ) );
    CHECK( approx( eye.y, -0.5f ) );
    // The target is the floor, in world levels; the eye is 0.8 above it in stretched space.
    CHECK( approx( eye.z, 0.1f * 1.8f + 0.8f ) );
}

TEST_CASE( "r3d_first_person_forward_eighths_is_the_heading", "[r3d]" )
{
    for( int h = 0; h < 8; ++h ) {
        CHECK( first_person_camera( h ).forward_eighths() == h );
    }
    r3d::camera cam = first_person_camera( 7 );
    cam.rotate_steps( 1 );
    CHECK( cam.heading == 0 );
    cam.rotate_steps( -1 );
    CHECK( cam.heading == 7 );
    cam.rotate_steps( -9 );
    CHECK( cam.heading == 6 );
    // Turning never touches the orbit angles.
    CHECK( approx( cam.yaw, 0.0f ) );
}

TEST_CASE( "r3d_first_person_centre_ray_points_along_the_heading", "[r3d]" )
{
    for( int h = 0; h < 8; ++h ) {
        const r3d::camera cam = first_person_camera( h );
        const r3d::ray ray = cam.screen_ray( 0.0f, 0.0f, 16.0f / 9.0f );
        // World space: heading 0 is +y (north), 2 is +x (east).
        const float angle = static_cast<float>( h ) * 0.78539816f;
        CHECK( approx( ray.dir.x, std::sin( angle ), 1e-3f ) );
        CHECK( approx( ray.dir.y, std::cos( angle ), 1e-3f ) );
        CHECK( approx( ray.dir.z, 0.0f, 1e-3f ) );
        // The ray starts at the eye (the near plane is a few centimetres in front of it). Rays are
        // in unstretched world space, eye() in stretched space.
        const r3d::vec3 eye = cam.eye();
        CHECK( r3d::length( ray.origin - r3d::vec3{ eye.x, eye.y, eye.z / cam.z_scale } ) < 0.1f );
    }
}

TEST_CASE( "r3d_first_person_right_is_clockwise_of_forward", "[r3d]" )
{
    for( int h = 0; h < 8; ++h ) {
        const r3d::camera cam = first_person_camera( h );
        const r3d::vec3 right = cam.ground_right();
        const r3d::vec3 fwd = cam.fp_forward();
        // forward is counter-clockwise of right seen from above (z up), so the cross product points up.
        CHECK( approx( right.x * fwd.y - right.y * fwd.x, 1.0f, 1e-3f ) );
        // A point to the right projects to the right half of the screen.
        const r3d::vec3 point = cam.eye() + cam.fp_forward() * 5.0f + right * 2.0f;
        const r3d::vec3 ndc = r3d::transform_point( cam.view_projection( 1.5f ), point );
        CHECK( ndc.x > 0.0f );
    }
}

TEST_CASE( "r3d_first_person_frustum_sees_ahead_not_behind", "[r3d]" )
{
    const r3d::camera cam = first_person_camera( 2 ); // facing east
    const r3d::frustum f = cam.view_frustum( 1.5f );
    const r3d::vec3 stretched_eye = cam.eye();
    const r3d::vec3 eye{ stretched_eye.x, stretched_eye.y, stretched_eye.z / cam.z_scale };
    const auto box_at = [ &eye ]( float dx, float dy ) {
        return r3d::aabb{ { eye.x + dx - 0.4f, eye.y + dy - 0.4f, eye.z - 0.4f },
            { eye.x + dx + 0.4f, eye.y + dy + 0.4f, eye.z + 0.4f } };
    };
    CHECK( f.intersects( box_at( 3.0f, 0.0f ) ) );
    CHECK_FALSE( f.intersects( box_at( -3.0f, 0.0f ) ) );
    // North and south of an east-facing viewer are outside a 90 degree field of view.
    CHECK_FALSE( f.intersects( box_at( 0.0f, 10.0f ) ) );
    CHECK_FALSE( f.intersects( box_at( 0.0f, -10.0f ) ) );
    // The wall of the neighbouring tile, half a tile away, is not cut off by the near plane.
    CHECK( f.intersects( box_at( 0.9f, 0.0f ) ) );
}

TEST_CASE( "r3d_first_person_projection_keeps_the_horizontal_field_of_view", "[r3d]" )
{
    r3d::camera cam = first_person_camera( 0 );
    cam.fovx = 1.5708f;
    for( float aspect : {
             1.0f, 1.6f, 2.4f
         } ) {
        // A point 5 ahead and 5 to the right sits at the right edge of a 90 degree view.
        const r3d::vec3 point = cam.eye() + r3d::vec3{ 5.0f, 5.0f, 0.0f };
        const r3d::vec3 ndc = r3d::transform_point( cam.view_projection( aspect ), point );
        CHECK( approx( ndc.x, 1.0f, 1e-2f ) );
        CHECK( approx( cam.fp_fovx( aspect ), 1.5708f ) );
    }
    // A tall, narrow picture keeps 90 degrees up and down instead (5 ahead, 5 up: the top
    // edge; z is stretched by z_scale in the view), and is narrower across.
    for( float aspect : {
             0.5f, 0.75f
         } ) {
        const r3d::vec3 point = cam.eye() + r3d::vec3{ 0.0f, 5.0f, 5.0f };
        const r3d::vec3 world{ point.x, point.y, point.z / cam.z_scale };
        const r3d::vec3 ndc = r3d::transform_point( cam.view_projection( aspect ), world );
        CHECK( approx( ndc.y, 1.0f, 1e-2f ) );
        CHECK( approx( cam.fp_fovy( aspect ), 1.5708f ) );
        CHECK( cam.fp_fovx( aspect ) < 1.5708f );
        const r3d::vec3 side = cam.eye() + r3d::vec3{ 5.0f * std::tan( cam.fp_fovx( aspect ) * 0.5f ),
                        5.0f, 0.0f };
        const r3d::vec3 side_world{ side.x, side.y, side.z / cam.z_scale };
        CHECK( approx( r3d::transform_point( cam.view_projection( aspect ), side_world ).x, 1.0f, 1e-2f ) );
    }
    // The orbit camera is untouched by first-person fields.
    r3d::camera orbit;
    CHECK( orbit.mode == r3d::camera_mode::orbit );
    CHECK( orbit.forward_eighths() == 0 );
}

TEST_CASE( "r3d_first_person_look_pitch_is_clamped", "[r3d]" )
{
    r3d::camera cam = first_person_camera( 0 );
    cam.pitch_steps( 50 );
    CHECK( approx( cam.look_pitch, r3d::camera::max_look_pitch ) );
    CHECK( cam.fp_forward().z > 0.9f );
    cam.pitch_steps( -100 );
    CHECK( approx( cam.look_pitch, -r3d::camera::max_look_pitch ) );
    const float distance = cam.distance;
    cam.zoom_steps( 5 );
    CHECK( approx( cam.distance, distance ) );
}

TEST_CASE( "r3d_first_person_pick_finds_the_tile_ahead", "[r3d]" )
{
    // Open floor of height 0.1 around tile (0, 0); the centre ray looking along the heading
    // lands on a floor tile ahead of the viewer.
    const r3d::column_height_fn floor_world = []( int, int, int z ) {
        return z == 0 ? r3d::floor_height : 0.0f;
    };
    // Looking down 45 degrees from an eye about 1 above the floor lands about 1 tile ahead.
    for( int h = 0; h < 8; ++h ) {
        r3d::camera cam = first_person_camera( h );
        cam.look_pitch = -0.7854f;
        const r3d::voxel_hit hit = r3d::pick_cell( cam, 0.0f, 0.0f, 1.5f, 400.0f, floor_world,
                                   r3d::floor_height );
        REQUIRE( hit.hit );
        const float angle = static_cast<float>( h ) * 0.78539816f;
        // Tile space: x east, y south.
        const int dx = static_cast<int>( std::lround( std::sin( angle ) ) );
        const int dy = static_cast<int>( std::lround( -std::cos( angle ) ) );
        CHECK( hit.x == dx );
        CHECK( hit.y == dy );
    }
}

TEST_CASE( "r3d_eighths_towards_names_the_nearest_heading", "[r3d]" )
{
    // Map convention: x east, y south; heading 0 is north, 2 east, 4 south, 6 west.
    CHECK( r3d::eighths_towards( 0, -3 ) == 0 );
    CHECK( r3d::eighths_towards( 2, -2 ) == 1 );
    CHECK( r3d::eighths_towards( 5, 0 ) == 2 );
    CHECK( r3d::eighths_towards( 1, 1 ) == 3 );
    CHECK( r3d::eighths_towards( 0, 4 ) == 4 );
    CHECK( r3d::eighths_towards( -1, 1 ) == 5 );
    CHECK( r3d::eighths_towards( -7, 0 ) == 6 );
    CHECK( r3d::eighths_towards( -1, -1 ) == 7 );
    // Off-axis offsets round to the closer of the two neighbouring headings.
    CHECK( r3d::eighths_towards( 1, -5 ) == 0 );
    CHECK( r3d::eighths_towards( 5, -1 ) == 2 );
    CHECK( r3d::eighths_towards( 0, 0 ) == 0 );
}

TEST_CASE( "r3d_visible_faces_adds_a_ceiling_under_a_solid_level_above", "[r3d]" )
{
    r3d::shape floor;
    floor.kind = r3d::shape_kind::floor;
    floor.height = r3d::floor_height;
    r3d::shape block;
    block.kind = r3d::shape_kind::block;
    block.height = 1.0f;
    r3d::shape prism;
    prism.kind = r3d::shape_kind::prism;
    prism.height = 0.5f;
    r3d::neighbourhood nb;
    // Open air above: sky, no ceiling.
    CHECK( ( r3d::visible_faces( floor, nb ) & r3d::face_ceiling ) == 0 );
    // A floor above (a room under a roof or a second storey): ceiling.
    nb.up = floor;
    CHECK( ( r3d::visible_faces( floor, nb ) & r3d::face_ceiling ) != 0 );
    CHECK( ( r3d::visible_faces( floor, nb ) & r3d::face_top ) != 0 );
    CHECK( ( r3d::visible_faces( prism, nb ) & r3d::face_ceiling ) != 0 );
    // A wall needs none inside itself.
    CHECK( ( r3d::visible_faces( block, nb ) & r3d::face_ceiling ) == 0 );
    // A wall above closes the room as well.
    nb.up = block;
    CHECK( ( r3d::visible_faces( floor, nb ) & r3d::face_ceiling ) != 0 );
}

TEST_CASE( "r3d_minimap_turns_the_heading_up", "[r3d]" )
{
    // Screen space: x right, y down; the player sits at (100, 100). A tile one step from them
    // in the direction they face must end up straight above them, whatever the heading.
    const float cx = 100.0f;
    const float cy = 100.0f;
    for( int h = 0; h < 8; ++h ) {
        const float angle = static_cast<float>( h ) * 0.78539816f;
        // North-up minimap: map x east is screen right, map y south is screen down.
        float x = cx + 10.0f * std::sin( angle );
        float y = cy - 10.0f * std::cos( angle );
        r3d::turn_on_screen( x, y, cx, cy, r3d::minimap_turn( h ) );
        CHECK( approx( x, cx ) );
        CHECK( approx( y, cy - 10.0f ) );
    }
    // Facing east: north swings to the left of the screen, south to the right.
    float nx = cx;
    float ny = cy - 10.0f;
    r3d::turn_on_screen( nx, ny, cx, cy, r3d::minimap_turn( 2 ) );
    CHECK( approx( nx, cx - 10.0f ) );
    CHECK( approx( ny, cy ) );
    // Heading north is no turn at all, and the player stays where they are.
    CHECK( approx( r3d::minimap_turn( 0 ), 0.0f ) );
    float px = cx;
    float py = cy;
    r3d::turn_on_screen( px, py, cx, cy, r3d::minimap_turn( 3 ) );
    CHECK( approx( px, cx ) );
    CHECK( approx( py, cy ) );
}

TEST_CASE( "r3d_map_bearing_is_clockwise_from_north_with_y_south", "[r3d]" )
{
    const float pi = 3.14159265f;
    // The map's y axis points south, so north is dy < 0.
    CHECK( approx( r3d::map_bearing( 0.0f, -1.0f ), 0.0f ) );
    CHECK( approx( r3d::map_bearing( 1.0f, -1.0f ), pi * 0.25f ) );
    CHECK( approx( r3d::map_bearing( 1.0f, 0.0f ), pi * 0.5f ) );
    CHECK( approx( r3d::map_bearing( 1.0f, 1.0f ), pi * 0.75f ) );
    CHECK( approx( std::fabs( r3d::map_bearing( 0.0f, 1.0f ) ), pi ) );
    CHECK( approx( r3d::map_bearing( -1.0f, 1.0f ), -pi * 0.75f ) );
    CHECK( approx( r3d::map_bearing( -1.0f, 0.0f ), -pi * 0.5f ) );
    CHECK( approx( r3d::map_bearing( -1.0f, -1.0f ), -pi * 0.25f ) );
    // The bearing of each heading matches the bearing of the step that heading walks, and the
    // first-person camera's view yaw for it (what the compass strip measures against).
    const int steps[8][2] = { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 } };
    for( int h = 0; h < 8; ++h ) {
        const float step = r3d::map_bearing( static_cast<float>( steps[h][0] ),
                                             static_cast<float>( steps[h][1] ) );
        CHECK( approx( r3d::wrap_angle( r3d::heading_bearing( h ) - step ), 0.0f ) );
        CHECK( r3d::eighths_towards( steps[h][0], steps[h][1] ) == h );
        r3d::camera cam;
        cam.mode = r3d::camera_mode::first_person;
        cam.heading = h;
        CHECK( approx( r3d::wrap_angle( cam.fp_yaw() - r3d::heading_bearing( h ) ), 0.0f ) );
        CHECK( cam.forward_eighths() == h );
    }
}

TEST_CASE( "r3d_relative_bearing_puts_left_negative_and_wraps", "[r3d]" )
{
    const float pi = 3.14159265f;
    for( int h = 0; h < 8; ++h ) {
        const float yaw = r3d::heading_bearing( h );
        // Straight ahead of each heading, in map space (y south).
        const float dx = std::sin( yaw );
        const float dy = -std::cos( yaw );
        CHECK( approx( r3d::relative_bearing( dx, dy, yaw ), 0.0f ) );
        // The offset turned a quarter clockwise on the map is at the right hand, and the other
        // way round at the left hand.
        CHECK( approx( r3d::relative_bearing( -dy, dx, yaw ), pi * 0.5f ) );
        CHECK( approx( r3d::relative_bearing( dy, -dx, yaw ), -pi * 0.5f ) );
        // Behind is +-pi, never outside that range.
        CHECK( approx( std::fabs( r3d::relative_bearing( -dx, -dy, yaw ) ), pi ) );
    }
    // Facing east (heading 2): something north is on the left, south on the right.
    const float east = r3d::heading_bearing( 2 );
    CHECK( r3d::relative_bearing( 0.0f, -3.0f, east ) < 0.0f );
    CHECK( r3d::relative_bearing( 0.0f, 3.0f, east ) > 0.0f );
    // Facing north-west (heading 7): something due west is half a quadrant to the left.
    CHECK( approx( r3d::relative_bearing( -2.0f, 0.0f, r3d::heading_bearing( 7 ) ), -pi * 0.25f ) );
    for( float a = -10.0f; a < 10.0f; a += 0.37f ) {
        const float w = r3d::wrap_angle( a );
        CHECK( w > -pi - 1e-4f );
        CHECK( w <= pi + 1e-4f );
        CHECK( approx( std::cos( w ), std::cos( a ) ) );
        CHECK( approx( std::sin( w ), std::sin( a ) ) );
    }
}

TEST_CASE( "r3d_compass_strip_places_bearings", "[r3d]" )
{
    const float pi = 3.14159265f;
    // A 200 pixel strip showing half a turn: ahead in the middle, the hands at the ends.
    CHECK( approx( r3d::compass_offset( 0.0f, 200.0f, pi ), 0.0f ) );
    CHECK( approx( r3d::compass_offset( pi * 0.5f, 200.0f, pi ), 100.0f ) );
    CHECK( approx( r3d::compass_offset( -pi * 0.5f, 200.0f, pi ), -100.0f ) );
    CHECK( approx( r3d::compass_offset( pi * 0.25f, 200.0f, pi ), 50.0f ) );
    // Behind: pinned to the nearer end.
    CHECK( approx( r3d::compass_offset( pi * 0.9f, 200.0f, pi ), 100.0f ) );
    CHECK( approx( r3d::compass_offset( -pi * 0.9f, 200.0f, pi ), -100.0f ) );
    CHECK( r3d::on_compass( pi * 0.5f, pi ) );
    CHECK_FALSE( r3d::on_compass( pi * 0.6f, pi ) );
    // The compass letters: facing east, N is at the left end and S at the right end.
    const float east = r3d::heading_bearing( 2 );
    CHECK( approx( r3d::compass_offset( r3d::wrap_angle( r3d::heading_bearing( 0 ) - east ), 200.0f,
                                        pi ), -100.0f ) );
    CHECK( approx( r3d::compass_offset( r3d::wrap_angle( r3d::heading_bearing( 4 ) - east ), 200.0f,
                                        pi ), 100.0f ) );
    CHECK_FALSE( r3d::on_compass( r3d::wrap_angle( r3d::heading_bearing( 6 ) - east ), pi ) );
}

TEST_CASE( "r3d_edge_alert_sectors", "[r3d]" )
{
    const float pi = 3.14159265f;
    const float fovx = pi * 0.5f; // 90 degrees
    using r3d::edge_side;
    // The eight neighbours of the player, indexed like the headings.
    const int steps[8][2] = { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 } };
    // By how many eighths clockwise the neighbour is from the heading -> the edge that lights.
    // The diagonals ahead are exactly on the picture's edge at 90 degrees, so they count as in it.
    const edge_side expected[8] = {
        edge_side::none, edge_side::none, edge_side::right, edge_side::right,
        edge_side::behind, edge_side::left, edge_side::left, edge_side::none
    };
    for( int h = 0; h < 8; ++h ) {
        const float yaw = r3d::heading_bearing( h );
        for( int n = 0; n < 8; ++n ) {
            const float rel = r3d::relative_bearing( static_cast<float>( steps[n][0] ),
                              static_cast<float>( steps[n][1] ), yaw );
            const int off = ( ( n - h ) % 8 + 8 ) % 8;
            INFO( "heading " << h << " neighbour " << n );
            CHECK( r3d::edge_alert_side( rel, fovx ) == expected[off] );
        }
    }
    // A wider view takes the side neighbours (90 degrees off) into the picture.
    CHECK( r3d::edge_alert_side( pi * 0.5f, pi * 0.9f ) == edge_side::right );
    CHECK( r3d::edge_alert_side( -pi * 0.5f, pi * 1.1f ) == edge_side::none );
}

TEST_CASE( "r3d_ceilings_close_everything_but_open_air", "[r3d]" )
{
    r3d::shape floor;
    floor.kind = r3d::shape_kind::floor;
    floor.height = r3d::floor_height;
    r3d::shape pane;
    pane.kind = r3d::shape_kind::pane;
    pane.height = 1.0f;
    r3d::shape table;
    table.kind = r3d::shape_kind::prism;
    table.height = 0.4f;
    table.slab = 0.08f;
    r3d::shape upright;
    upright.kind = r3d::shape_kind::upright;
    upright.height = 1.4f;
    r3d::neighbourhood nb;
    // A window or door in a wall line gets the room's ceiling under a floor above.
    nb.up = floor;
    CHECK( ( r3d::visible_faces( pane, nb ) & r3d::face_ceiling ) != 0 );
    // It has no other faces: the mesher draws the frame itself.
    CHECK( ( r3d::visible_faces( pane, nb ) & ~r3d::face_ceiling ) == 0 );
    // A window pane, a table or a shrub upstairs leaves no hole in the ceiling below.
    for( const r3d::shape &up : {
             pane, table, upright
         } ) {
        nb.up = up;
        CHECK( ( r3d::visible_faces( floor, nb ) & r3d::face_ceiling ) != 0 );
    }
    // Trees reach into the level above: never a ceiling over one.
    nb.up = floor;
    CHECK( ( r3d::visible_faces( upright, nb ) & r3d::face_ceiling ) == 0 );
    // Open air above a window: sky.
    nb.up = r3d::shape();
    CHECK( ( r3d::visible_faces( pane, nb ) & r3d::face_ceiling ) == 0 );
}

TEST_CASE( "r3d_first_person_level_tilt_adds_to_the_look_pitch", "[r3d]" )
{
    r3d::camera cam;
    cam.mode = r3d::camera_mode::first_person;
    cam.heading = 2;
    // Looking one level up tilts the view 45 degrees up, still facing the heading.
    cam.tilt_pitch = 0.7854f;
    const r3d::vec3 up = cam.fp_forward();
    CHECK( approx( up.z, std::sin( 0.7854f ) ) );
    CHECK( up.x > 0.0f );
    CHECK( approx( up.y, 0.0f ) );
    // Together with a look up it stays within the clamp.
    cam.look_pitch = 0.7854f;
    CHECK( approx( cam.fp_forward().z, std::sin( r3d::camera::max_look_pitch ) ) );
    // Looking a level down tilts the view down; walking directions keep the heading.
    cam.look_pitch = 0.0f;
    cam.tilt_pitch = -0.7854f;
    CHECK( cam.fp_forward().z < 0.0f );
    CHECK( cam.forward_eighths() == 2 );
}

TEST_CASE( "r3d_minimap_disc_stays_whole_when_turned", "[r3d]" )
{
    // The heading-up minimap keeps only cells within a disc around the player that fits in the
    // drawn square; turning keeps distances, so every kept cell stays inside the square at every
    // heading and the square is never left with an empty corner inside the disc.
    const float cx = 50.0f;
    const float cy = 60.0f;
    const float radius = 40.0f;
    for( int h = 0; h < 8; ++h ) {
        for( float a = 0.0f; a < 6.28f; a += 0.3f ) {
            for( float d = 0.0f; d <= radius; d += 7.5f ) {
                float x = cx + d * std::cos( a );
                float y = cy + d * std::sin( a );
                r3d::turn_on_screen( x, y, cx, cy, r3d::minimap_turn( h ) );
                CHECK( approx( std::hypot( x - cx, y - cy ), d ) );
                CHECK( std::fabs( x - cx ) <= radius + 1e-3f );
                CHECK( std::fabs( y - cy ) <= radius + 1e-3f );
            }
        }
    }
}

TEST_CASE( "r3d_clear_line_is_blocked_by_walls_but_not_by_the_target", "[r3d]" )
{
    // Floors everywhere on level 0, a wall at (3, 0), a counter at (5, 2).
    const r3d::column_height_fn world = []( int x, int y, int z ) {
        if( z != 0 ) {
            return 0.0f;
        }
        if( x == 3 && y == 0 ) {
            return 1.0f;
        }
        if( x == 5 && y == 2 ) {
            return 0.4f;
        }
        return r3d::floor_height;
    };
    const r3d::vec3 eye{ 0.5f, 0.5f, 0.55f };
    // Behind the wall: hidden. In front of it: seen.
    CHECK_FALSE( r3d::clear_line( eye, { 6.5f, 0.5f, 0.5f }, world ) );
    CHECK( r3d::clear_line( eye, { 2.5f, 0.5f, 0.5f }, world ) );
    // Off to the side of the wall: seen.
    CHECK( r3d::clear_line( eye, { 4.5f, 3.5f, 0.5f }, world ) );
    // Standing on the counter: its own column does not hide it.
    CHECK( r3d::clear_line( eye, { 5.5f, 2.5f, 0.6f }, world ) );
    // The same point as the eye is trivially seen.
    CHECK( r3d::clear_line( eye, eye, world ) );
}

TEST_CASE( "r3d_sprites_slide_away_from_the_eye", "[r3d]" )
{
    // Eye at (0, 0) looking +x.
    // A sprite 0.2 ahead slides to 0.35.
    CHECK( approx( r3d::slide_from_eye( 0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.35f ), 0.15f ) );
    // Off to the side, its depth along the view counts, not its distance.
    CHECK( approx( r3d::slide_from_eye( 0.1f, 0.4f, 0.0f, 0.0f, 1.0f, 0.0f, 0.35f ), 0.25f ) );
    // Far enough already, or behind the eye: left alone.
    CHECK( approx( r3d::slide_from_eye( 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.35f ), 0.0f ) );
    CHECK( approx( r3d::slide_from_eye( -0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.35f ), 0.0f ) );
    // A neighbour seen from the tile centre (a whole tile away) never slides, at any heading.
    for( int h = 0; h < 8; ++h ) {
        const float a = static_cast<float>( h ) * 0.78539816f;
        const float fx = std::sin( a );
        const float fy = -std::cos( a );
        CHECK( approx( r3d::slide_from_eye( 0.5f + fx * 1.0f, 0.5f + fy * 1.0f, 0.5f, 0.5f, fx, fy,
                                            0.35f ), 0.0f ) );
    }
    // An eye off its tile centre and close to a sprite (any frame): it slides.
    CHECK( r3d::slide_from_eye( 1.5f, 0.5f, 1.3f, 0.5f, 1.0f, 0.0f, 0.35f ) > 0.0f );
}
