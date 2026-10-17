// 3D renderer: samples the tileset sheet and applies the baked per-vertex light. Sprites with
// transparent pixels are alpha tested so depth writes stay correct.

#version 450

layout(set = 2, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_light;
layout(location = 2) in vec3 v_world;
layout(location = 3) flat in vec4 v_cut;
layout(location = 4) flat in vec4 v_cut2;
layout(location = 5) flat in float v_stub;
layout(location = 0) out vec4 out_color;

void main()
{
    // Cutaway: whatever stands between the camera and the player (a wall, a locker) is cut down
    // to a low stub, so they are never hidden. The test is on the fragment's tile, so a tile
    // goes as a whole and never shows its far faces: tiles whose centre lies nearer the camera
    // than the player, inside an ellipse around them that is as long along the view as it looks
    // wide across it. Caps flagged as stubs are drawn only there, so the cut tile keeps a top.
    bool inside = false;
    if (v_cut.w > 0.0) {
        vec2 d = floor(v_world.xy) + 0.5 - v_cut.xy;
        float along = dot(d, v_cut2.xy);
        float across = dot(d, vec2(v_cut2.y, -v_cut2.x));
        float e = (across * across) / (v_cut.w * v_cut.w) + (along * along) / (v_cut2.z * v_cut2.z);
        inside = along < -0.25 && e < 1.0;
    }
    if (v_stub > 0.5) {
        if (!inside) {
            discard;
        }
    } else if (inside && v_world.z > v_cut.z) {
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
    out_color = vec4(texel.rgb * v_light.rgb, 1.0);
}
