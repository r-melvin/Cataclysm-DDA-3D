#include "r3d_math.h"

#include <utility>

namespace r3d
{

mat4 operator*( const mat4 &a, const mat4 &b )
{
    mat4 out;
    for( int col = 0; col < 4; ++col ) {
        for( int row = 0; row < 4; ++row ) {
            float sum = 0.0f;
            for( int k = 0; k < 4; ++k ) {
                sum += a.at( row, k ) * b.at( k, col );
            }
            out.at( row, col ) = sum;
        }
    }
    return out;
}

vec3 transform_point( const mat4 &mat, const vec3 &p, float *w_out )
{
    const float x = mat.at( 0, 0 ) * p.x + mat.at( 0, 1 ) * p.y + mat.at( 0, 2 ) * p.z + mat.at( 0, 3 );
    const float y = mat.at( 1, 0 ) * p.x + mat.at( 1, 1 ) * p.y + mat.at( 1, 2 ) * p.z + mat.at( 1, 3 );
    const float z = mat.at( 2, 0 ) * p.x + mat.at( 2, 1 ) * p.y + mat.at( 2, 2 ) * p.z + mat.at( 2, 3 );
    const float w = mat.at( 3, 0 ) * p.x + mat.at( 3, 1 ) * p.y + mat.at( 3, 2 ) * p.z + mat.at( 3, 3 );
    if( w_out ) {
        *w_out = w;
    }
    if( w != 0.0f && w != 1.0f ) {
        return { x / w, y / w, z / w };
    }
    return { x, y, z };
}

mat4 look_at( const vec3 &eye, const vec3 &target, const vec3 &up )
{
    const vec3 f = normalize( target - eye );
    const vec3 s = normalize( cross( f, up ) );
    const vec3 u = cross( s, f );
    mat4 out;
    out.at( 0, 0 ) = s.x;
    out.at( 0, 1 ) = s.y;
    out.at( 0, 2 ) = s.z;
    out.at( 0, 3 ) = -dot( s, eye );
    out.at( 1, 0 ) = u.x;
    out.at( 1, 1 ) = u.y;
    out.at( 1, 2 ) = u.z;
    out.at( 1, 3 ) = -dot( u, eye );
    out.at( 2, 0 ) = -f.x;
    out.at( 2, 1 ) = -f.y;
    out.at( 2, 2 ) = -f.z;
    out.at( 2, 3 ) = dot( f, eye );
    return out;
}

mat4 perspective( float fovy_rad, float aspect, float near_plane, float far_plane )
{
    const float f = 1.0f / std::tan( fovy_rad * 0.5f );
    mat4 out;
    out.m.fill( 0.0f );
    out.at( 0, 0 ) = f / aspect;
    out.at( 1, 1 ) = f;
    out.at( 2, 2 ) = far_plane / ( near_plane - far_plane );
    out.at( 2, 3 ) = near_plane * far_plane / ( near_plane - far_plane );
    out.at( 3, 2 ) = -1.0f;
    return out;
}

mat4 orthographic( float half_height, float aspect, float near_plane, float far_plane )
{
    const float half_width = half_height * aspect;
    mat4 out;
    out.m.fill( 0.0f );
    out.at( 0, 0 ) = 1.0f / half_width;
    out.at( 1, 1 ) = 1.0f / half_height;
    out.at( 2, 2 ) = 1.0f / ( near_plane - far_plane );
    out.at( 2, 3 ) = near_plane / ( near_plane - far_plane );
    out.at( 3, 3 ) = 1.0f;
    return out;
}

bool invert( const mat4 &in, mat4 &out )
{
    // Gauss-Jordan elimination with partial pivoting on a double-precision copy.
    double a[4][8];
    for( int r = 0; r < 4; ++r ) {
        for( int c = 0; c < 4; ++c ) {
            a[r][c] = in.at( r, c );
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    }
    for( int col = 0; col < 4; ++col ) {
        int pivot = col;
        for( int r = col + 1; r < 4; ++r ) {
            if( std::fabs( a[r][col] ) > std::fabs( a[pivot][col] ) ) {
                pivot = r;
            }
        }
        if( std::fabs( a[pivot][col] ) < 1e-12 ) {
            return false;
        }
        if( pivot != col ) {
            for( int c = 0; c < 8; ++c ) {
                std::swap( a[pivot][c], a[col][c] );
            }
        }
        const double inv = 1.0 / a[col][col];
        for( int c = 0; c < 8; ++c ) {
            a[col][c] *= inv;
        }
        for( int r = 0; r < 4; ++r ) {
            if( r == col ) {
                continue;
            }
            const double factor = a[r][col];
            for( int c = 0; c < 8; ++c ) {
                a[r][c] -= factor * a[col][c];
            }
        }
    }
    for( int r = 0; r < 4; ++r ) {
        for( int c = 0; c < 4; ++c ) {
            out.at( r, c ) = static_cast<float>( a[r][c + 4] );
        }
    }
    return true;
}

} // namespace r3d
