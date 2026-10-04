#version 450

layout(location = 0) in vec3 inPosition;

// Раскладка обязана совпадать со структурой Transform в application.cpp:
// mat4 занимает 64 байта, vec3 выравнивается до 16 байт, поэтому блок — 76 байт.
layout(push_constant) uniform PushConstants {
	mat4 modelViewProjection;
	vec3 color;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
	gl_Position = pc.modelViewProjection * vec4(inPosition, 1.0);
	fragColor = pc.color;
}