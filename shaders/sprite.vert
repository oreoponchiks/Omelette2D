#version 450

layout(location = 0) in vec4 inRectangle;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 color;

layout(push_constant) uniform Screen { vec2 size; } screen;

void main() {
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 position = inRectangle.xy + corners[gl_VertexIndex] * inRectangle.zw;
    vec2 ndc = vec2(position.x / screen.size.x * 2.0 - 1.0,
                    position.y / screen.size.y * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    color = inColor;
}
