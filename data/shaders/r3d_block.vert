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
    vec4 pull;   // xyz: direction towards the camera; w: depth pull at light alpha 1
    vec4 cut;    // xy: the player in world space, z: height below which nothing is cut,
                 // w: cutaway half-width across the view, in tiles (0: off)
    vec4 cut2;   // xy: the view's forward direction on the ground, z: cutaway half-length
                 // along it
} u;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_light;
layout(location = 2) out vec3 v_world;
layout(location = 3) flat out vec4 v_cut;
layout(location = 4) flat out vec4 v_cut2;
layout(location = 5) flat out float v_stub;

void main()
{
    v_uv = a_uv;
    v_light = vec4(a_light.rgb, 1.0);
    vec3 pos = a_pos + u.offset.xyz;
    vec4 clip = u.view_proj * vec4(pos, 1.0);
    // Camera-facing billboards lean their top away from the camera, into whatever stands behind
    // their tile. Their depth is taken from a point slid towards the camera (by the alpha of the
    // light, so the base stays put) while the screen position stays that of the real point.
    if (u.pull.w > 0.0) {
        vec4 pulled = u.view_proj * vec4(pos + u.pull.xyz * (u.pull.w * a_light.a), 1.0);
        clip.z = pulled.z / pulled.w * clip.w;
    }
    gl_Position = clip;
    v_world = pos;
    v_cut = u.cut;
    v_cut2 = u.cut2;
    // In the terrain pass (no billboard pull) the light's alpha flags a cap that is only drawn
    // inside the cutaway, where the tile above it is removed.
    v_stub = u.pull.w > 0.0 ? 0.0 : a_light.a;
}
