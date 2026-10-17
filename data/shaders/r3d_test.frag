// M0 spike fragment shader: checker pattern tinted by the quad colour.

#version 450

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 cell = floor(v_uv * 8.0);
    float checker = mod(cell.x + cell.y, 2.0);
    out_color = vec4(v_color * mix(0.55, 1.0, checker), 1.0);
}
