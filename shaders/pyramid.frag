#version 450

layout(location = 0) in vec3 fragColor;

// Цвет из интерфейса — множитель на процедурный цвет вершины.
layout(push_constant) uniform PushConstants {
	mat4 modelViewProjection;
	vec3 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = vec4(fragColor * pc.color, 1.0);
}