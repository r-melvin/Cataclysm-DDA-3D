#include "r3d_dda.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace r3d
{

namespace
{

struct box_hit {
    bool hit = false;
    float t = 0.0f;
    hit_face face = hit_face::none;
};

// Ray vs. axis-aligned box using the slab method. A ray that starts inside the box hits at t = 0
// with face none. Only hits with t <= max_t count.
box_hit intersect_box( const vec3 &o, const vec3 &d, const vec3 &lo, const vec3 &hi, float max_t )
{
    float t_near = -std::numeric_limits<float>::infinity();
    float t_far = std::numeric_limits<float>::infinity();
    hit_face face = hit_face::none;

    const float origin[3] = { o.x, o.y, o.z };
    const float dir[3] = { d.x, d.y, d.z };
    const float lo_a[3] = { lo.x, lo.y, lo.z };
    const float hi_a[3] = { hi.x, hi.y, hi.z };
    // Entry face when moving in the positive / negative direction along each axis.
    const hit_face enter_pos[3] = { hit_face::west, hit_face::north, hit_face::bottom };
    const hit_face enter_neg[3] = { hit_face::east, hit_face::south, hit_face::top };

    for( int axis = 0; axis < 3; ++axis ) {
        if( dir[axis] == 0.0f ) {
            if( origin[axis] < lo_a[axis] || origin[axis] > hi_a[axis] ) {
                return {};
            }
            continue;
        }
        const float inv = 1.0f / dir[axis];
        float t0 = ( lo_a[axis] - origin[axis] ) * inv;
        float t1 = ( hi_a[axis] - origin[axis] ) * inv;
        hit_face entered = enter_pos[axis];
        if( t0 > t1 ) {
            std::swap( t0, t1 );
            entered = enter_neg[axis];
        }
        if( t0 > t_near ) {
            t_near = t0;
            face = entered;
        }
        t_far = std::min( t_far, t1 );
        if( t_near > t_far ) {
            return {};
        }
    }
    if( t_far < 0.0f ) {
        return {};
    }
    box_hit out;
    if( t_near < 0.0f ) {
        out.t = 0.0f;
        out.face = hit_face::none;
    } else {
        out.t = t_near;
        out.face = face;
    }
    out.hit = out.t <= max_t;
    return out;
}

} // namespace

voxel_hit raycast_columns( const vec3 &origin, const vec3 &dir, float max_t,
                           const column_height_fn &height )
{
    int cell[3] = { static_cast<int>( std::floor( origin.x ) ),
                    static_cast<int>( std::floor( origin.y ) ),
                    static_cast<int>( std::floor( origin.z ) )
                  };
    const float o[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { dir.x, dir.y, dir.z };
    int step[3];
    float t_max[3];
    float t_delta[3];
    for( int axis = 0; axis < 3; ++axis ) {
        if( d[axis] > 0.0f ) {
            step[axis] = 1;
            t_delta[axis] = 1.0f / d[axis];
            t_max[axis] = ( static_cast<float>( cell[axis] + 1 ) - o[axis] ) * t_delta[axis];
        } else if( d[axis] < 0.0f ) {
            step[axis] = -1;
            t_delta[axis] = -1.0f / d[axis];
            t_max[axis] = ( o[axis] - static_cast<float>( cell[axis] ) ) * t_delta[axis];
        } else {
            step[axis] = 0;
            t_delta[axis] = std::numeric_limits<float>::infinity();
            t_max[axis] = std::numeric_limits<float>::infinity();
        }
    }

    // The cell the ray starts in is visited first; afterwards we step across the nearest boundary.
    float t_enter = 0.0f;
    while( t_enter <= max_t ) {
        const float h = height( cell[0], cell[1], cell[2] );
        if( h > 0.0f ) {
            const vec3 lo{ static_cast<float>( cell[0] ), static_cast<float>( cell[1] ),
                           static_cast<float>( cell[2] ) };
            const vec3 hi{ lo.x + 1.0f, lo.y + 1.0f, lo.z + std::min( h, 1.0f ) };
            const box_hit bh = intersect_box( origin, dir, lo, hi, max_t );
            if( bh.hit ) {
                return { true, cell[0], cell[1], cell[2], bh.t, bh.face };
            }
        }
        int axis = 0;
        if( t_max[1] < t_max[axis] ) {
            axis = 1;
        }
        if( t_max[2] < t_max[axis] ) {
            axis = 2;
        }
        if( step[axis] == 0 || std::isinf( t_max[axis] ) ) {
            break;
        }
        t_enter = t_max[axis];
        cell[axis] += step[axis];
        t_max[axis] += t_delta[axis];
    }
    return {};
}

voxel_hit raycast_columns_reference( const vec3 &origin, const vec3 &dir, float max_t,
                                     const column_height_fn &height,
                                     int lo_x, int lo_y, int lo_z, int hi_x, int hi_y, int hi_z )
{
    voxel_hit best;
    for( int x = lo_x; x <= hi_x; ++x ) {
        for( int y = lo_y; y <= hi_y; ++y ) {
            for( int z = lo_z; z <= hi_z; ++z ) {
                const float h = height( x, y, z );
                if( h <= 0.0f ) {
                    continue;
                }
                const vec3 lo{ static_cast<float>( x ), static_cast<float>( y ), static_cast<float>( z ) };
                const vec3 hi{ lo.x + 1.0f, lo.y + 1.0f, lo.z + std::min( h, 1.0f ) };
                const box_hit bh = intersect_box( origin, dir, lo, hi, max_t );
                if( bh.hit && ( !best.hit || bh.t < best.t ) ) {
                    best = { true, x, y, z, bh.t, bh.face };
                }
            }
        }
    }
    return best;
}

} // namespace r3d
