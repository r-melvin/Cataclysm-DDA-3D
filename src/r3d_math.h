#pragma once
#ifndef CATA_SRC_R3D_MATH_H
#define CATA_SRC_R3D_MATH_H

#include <array>
#include <cmath>

// Small float vector/matrix helpers for the 3D renderer. Plain C++, no SDL, so the camera and
// picking code can be unit tested in every build.
//
// World space is right-handed: x east, y north, z up, one unit per tile. Tile coordinates have
// y growing southwards, so world y = -tile y (see r3d::camera for the conversion helpers).
// Matrices are column-major (m[column * 4 + row]) and map to clip space with y up and depth in
// [0, 1], which is what SDL_GPU expects.

namespace r3d
{

struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline vec3 operator+( const vec3 &a, const vec3 &b )
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline vec3 operator-( const vec3 &a, const vec3 &b )
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline vec3 operator*( const vec3 &a, float s )
{
    return { a.x * s, a.y * s, a.z * s };
}
inline float dot( const vec3 &a, const vec3 &b )
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline vec3 cross( const vec3 &a, const vec3 &b )
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float length( const vec3 &a )
{
    return std::sqrt( dot( a, a ) );
}
inline vec3 normalize( const vec3 &a )
{
    const float len = length( a );
    return len > 0.0f ? a * ( 1.0f / len ) : vec3{};
}

struct mat4 {
    std::array<float, 16> m{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    float &at( int row, int col ) {
        return m[col * 4 + row];
    }
    float at( int row, int col ) const {
        return m[col * 4 + row];
    }
};

mat4 operator*( const mat4 &a, const mat4 &b );

/** Transforms a point (w = 1) and returns the resulting clip-space w through *w_out if given. */
vec3 transform_point( const mat4 &mat, const vec3 &p, float *w_out = nullptr );

/** Right-handed view matrix looking from eye at target. */
mat4 look_at( const vec3 &eye, const vec3 &target, const vec3 &up );

/** Right-handed perspective projection with depth in [0, 1]. */
mat4 perspective( float fovy_rad, float aspect, float near_plane, float far_plane );

/** Right-handed orthographic projection with depth in [0, 1]; half_height is in world units. */
mat4 orthographic( float half_height, float aspect, float near_plane, float far_plane );

/** General 4x4 inverse; returns false if the matrix is singular. */
bool invert( const mat4 &in, mat4 &out );

} // namespace r3d

#endif // CATA_SRC_R3D_MATH_H
