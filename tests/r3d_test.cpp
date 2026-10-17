#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "cata_catch.h"
#include "r3d_camera.h"
#include "r3d_dda.h"
#include "r3d_math.h"
#include "r3d_shapes.h"

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

    // An empty or broken window frame is passable and stands as a pane; an intact one blocks.
    t = {};
    t.window = true;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::pane );
    t.move_cost = 0;
    CHECK( r3d::classify_terrain( t ).kind == r3d::shape_kind::block );
}

TEST_CASE( "r3d_furniture_shapes", "[r3d]" )
{
    // Ordinary walkable furniture (chairs, beds) is a sprite on top, not a box: its sprite has
    // transparent margins that would make a hollow shell.
    r3d::tile_traits t;
    const r3d::shape ordinary = r3d::classify_furniture( t );
    CHECK( ordinary.kind == r3d::shape_kind::floor );
    CHECK( ordinary.height > r3d::floor_height );

    t = {};
    t.tiny = true;
    CHECK( r3d::classify_furniture( t ).height < ordinary.height );

    // Tall impassable furniture (lockers, fridges) stands upright and is taller than the default.
    t = {};
    t.move_cost = 0;
    const r3d::shape locker = r3d::classify_furniture( t );
    CHECK( locker.kind == r3d::shape_kind::upright );
    CHECK( locker.height > ordinary.height );

    // Impassable but low or flat-topped things (counters, low walls) stay top-only.
    t = {};
    t.move_cost = 0;
    t.flat_surface = true;
    CHECK( r3d::classify_furniture( t ).kind == r3d::shape_kind::floor );

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

    // Block with another block to the east and above: those two faces are hidden.
    r3d::neighbourhood packed;
    packed.east = block;
    packed.up = block;
    const uint8_t faces = r3d::visible_faces( block, packed );
    CHECK( ( faces & r3d::face_east ) == 0 );
    CHECK( ( faces & r3d::face_top ) == 0 );
    CHECK( ( faces & r3d::face_west ) != 0 );

    // A block beside a floor still shows that side, because the floor is lower.
    r3d::neighbourhood beside_floor;
    beside_floor.south = floor;
    CHECK( ( r3d::visible_faces( block, beside_floor ) & r3d::face_south ) != 0 );

    // A floor only ever has a top face, and loses it under a block.
    CHECK( r3d::visible_faces( floor, open ) == r3d::face_top );
    r3d::neighbourhood roofed;
    roofed.up = block;
    CHECK( r3d::visible_faces( floor, roofed ) == 0 );

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
