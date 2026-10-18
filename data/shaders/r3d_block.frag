// 3D renderer: samples the tileset sheet and applies the baked per-vertex light. Sprites with
// transparent pixels are alpha tested so depth writes stay correct.

#version 450

layout(set = 2, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_light;
layout(location = 2) noperspective in vec3 v_ndc;
layout(location = 3) in float v_height;
layout(location = 4) flat in vec4 v_cut;
layout(location = 5) flat in vec2 v_cut2;
layout(location = 0) out vec4 out_color;

void main()
{
    // Cutaway: whatever stands between the camera and the player (a wall, a locker) is left out
    // above knee height in a circle around them on screen, so they are never hidden. Back faces
    // are culled, so the hole shows the floor and the cut ends of the walls, not their insides.
    if (v_cut.w > 0.0 && v_height > v_cut2.y && v_ndc.z < v_cut.z) {
        vec2 d = vec2(v_ndc.x - v_cut.x, (v_ndc.y - v_cut.y) * v_cut2.x);
        if (dot(d, d) < v_cut.w * v_cut.w) {
            discard;
        }
    }
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
