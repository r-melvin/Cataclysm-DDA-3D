// 3D renderer: samples the tileset sheet and applies the baked per-vertex light. Sprites with
// transparent pixels are alpha tested so depth writes stay correct.

#version 450

layout(set = 2, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_light;
layout(location = 2) in vec3 v_world;
layout(location = 3) flat in vec4 v_cut;
layout(location = 4) flat in vec4 v_cut2;
layout(location = 5) flat in vec2 v_tile;
layout(location = 0) out vec4 out_color;

void main()
{
    // Cutaway: whatever stands between the camera and the player (a wall, a locker) is left out
    // of the opaque pass above knee height, so they are never hidden, and drawn again by the
    // ghost pass (cut2.w = 1) as a see-through layer. The test is on the tile the face belongs
    // to, so a tile goes as a whole, back faces included: tiles whose centre lies nearer the camera than the player, inside an
    // ellipse around them that is as long along the view as it looks wide across it.
    bool cut = false;
    if (v_cut.w > 0.0) {
        vec2 d = v_tile - v_cut.xy;
        float along = dot(d, v_cut2.xy);
        float across = dot(d, vec2(v_cut2.y, -v_cut2.x));
        cut = along < -0.25 && along > -v_cut2.z && abs(across) < v_cut.w && v_world.z > v_cut.z;
    }
    const bool ghost_pass = v_cut2.w > 0.5;
    if (cut != ghost_pass) {
        discard;
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
    out_color = vec4(texel.rgb * v_light.rgb, ghost_pass ? 0.35 : 1.0);
}
