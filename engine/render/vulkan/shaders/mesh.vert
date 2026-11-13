#version 450

struct Light {
  vec4 positionType;
  vec4 directionRange;
  vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform Frame {
  mat4 view;
  mat4 proj;
  vec4 eye;
  vec4 lightCount;
  Light lights[8];
} frame;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 baseColorFactor;
  vec4 emissiveNormalScale;
  vec4 materialParams;
  vec4 reserved;
} pc;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 worldPos;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;

void main() {
  vec4 world = pc.model * vec4(position, 1.0);
  worldPos = world.xyz;
  worldNormal = mat3(pc.model) * normal;
  texCoord = uv;
  gl_Position = frame.proj * frame.view * world;
}
