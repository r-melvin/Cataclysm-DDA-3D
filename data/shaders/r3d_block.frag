// 3D renderer: samples the tileset sheet and applies the baked per-vertex light. Sprites with
// transparent pixels are alpha tested so depth writes stay correct.

#version 450

layout(set = 2, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_light;
layout(location = 0) out vec4 out_color;

void main()
{
    vec4 texel = texture(u_atlas, v_uv);
    // Tilesets draw drop shadows as half-transparent black; the 3D view has no blending, so
    // they are left out rather than drawn as solid black shapes.
    if (texel.a < 0.75) {
        discard;
    }
    // Places never seen are lit black: leave them out so they do not hide what is seen behind
    // them, as the flat view simply draws nothing there.
    if (max(v_light.r, max(v_light.g, v_light.b)) < 0.5 / 255.0) {
        discard;
    }
    out_color = vec4(texel.rgb * v_light.rgb, 1.0);
}
