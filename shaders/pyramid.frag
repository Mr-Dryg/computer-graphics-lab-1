#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_world_position;

layout(location = 0) out vec4 out_frag_color;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color_mult;
} pc;

void main() {
    vec3 normal = normalize(in_normal);

    vec3 light_dir = normalize(vec3(0.5, 1.0, 0.3));
    float diffuse = max(dot(normal, light_dir), 0.0);
    float ambient = 0.25;

    vec3 base_color = in_color * pc.color_mult.rgb;
    vec3 color = base_color * (ambient + diffuse);

    out_frag_color = vec4(color, pc.color_mult.a);
}