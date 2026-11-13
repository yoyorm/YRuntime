#version 450

layout(push_constant) uniform Push {
  mat4 model;
  mat4 lightViewProj;
} pc;

layout(location = 0) in vec3 position;

void main() {
  gl_Position = pc.lightViewProj * pc.model * vec4(position, 1.0);
}
