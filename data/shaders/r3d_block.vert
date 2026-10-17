// 3D renderer: textured geometry (terrain blocks, furniture, billboards).
// Stream 0 holds position and uv, stream 1 holds a per-vertex light colour that can be
// re-uploaded without touching the geometry.

#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_light;

layout(set = 1, binding = 0) uniform params {
    mat4 view_proj;
    vec4 offset; // xyz: translation applied to a_pos before projection
} u;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_light;

void main()
{
    v_uv = a_uv;
    v_light = a_light;
    gl_Position = u.view_proj * vec4(a_pos + u.offset.xyz, 1.0);
}
