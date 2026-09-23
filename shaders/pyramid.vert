#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_color;
layout(location = 2) out vec3 out_world_position;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color_mult;
} pc;

layout(set = 0, binding = 0) uniform ObjectUniforms {
    mat4 mvp;
} ubo;

void main() {
    gl_Position = ubo.mvp * vec4(in_position, 1.0);
    out_normal = mat3(pc.model) * in_normal;
    out_color = in_color;
    out_world_position = (pc.model * vec4(in_position, 1.0)).xyz;
}