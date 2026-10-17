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
    vec4 cut;    // xy: the player on screen (NDC), z: their depth, w: cutaway radius (0: off)
    vec4 cut2;   // x: NDC y to NDC x scale, y: world height below which nothing is cut
    vec4 fog;    // rgb: fog colour, w: density (0: no fog)
    vec4 fog2;   // xy: where fog distance is measured from, in the frame of pos; z: 1 to
                 // draw translucent texels dithered (weather particles)
} u;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_light;
layout(location = 2) noperspective out vec3 v_ndc;
layout(location = 3) out float v_height;
layout(location = 4) flat out vec4 v_cut;
layout(location = 5) flat out vec2 v_cut2;
layout(location = 6) out float v_fog;
layout(location = 7) flat out vec4 v_fog_color; // w: dithered transparency (particles)

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
    v_ndc = clip.xyz / clip.w;
    v_height = pos.z;
    v_cut = u.cut;
    v_cut2 = u.cut2.xy;
    // Weather fog grows with the distance over the ground from the viewed place.
    float d = length(pos.xy - u.fog2.xy) * u.fog.w;
    v_fog = 1.0 - exp(-d * d);
    v_fog_color = vec4(u.fog.rgb, u.fog2.z);
}
