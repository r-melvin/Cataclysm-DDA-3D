// M0 spike vertex shader for the 3D renderer. Emits two overlapping quads from
// gl_VertexIndex with no vertex buffer so the spike can prove depth testing:
// quad 0 spins at depth 0.3 and is drawn first; quad 1 is static, farther at depth
// 0.6 and drawn second, so it must stay hidden behind quad 0 if depth testing works.

#version 450

layout(set = 1, binding = 0) uniform params {
    float angle;
    float aspect;
} u;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_uv;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    int quad = gl_VertexIndex / 6;
    vec2 c = corners[gl_VertexIndex % 6];
    v_uv = c * 0.5 + 0.5;

    vec2 p;
    float depth;
    if (quad == 0) {
        float s = sin(u.angle);
        float co = cos(u.angle);
        p = mat2(co, s, -s, co) * c * 0.6;
        depth = 0.3;
        v_color = vec3(0.9, 0.3, 0.2);
    } else {
        p = c * 0.25 + vec2(0.1, 0.0);
        depth = 0.6;
        v_color = vec3(0.2, 0.5, 0.9);
    }
    p.x /= u.aspect;
    gl_Position = vec4(p, depth, 1.0);
}
