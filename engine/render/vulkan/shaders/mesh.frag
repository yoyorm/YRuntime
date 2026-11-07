#version 450

layout(set = 0, binding = 0) uniform Frame {
  mat4 view;
  mat4 proj;
  vec4 eye;
  vec4 lightDir;
  vec4 lightColor;
} frame;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 baseColorFactor;
  vec4 materialParams;
} pc;

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
  vec3 albedo = pc.baseColorFactor.rgb;
  vec3 N = normalize(worldNormal);
  vec3 L = normalize(frame.lightDir.xyz);
  vec3 V = normalize(frame.eye.xyz - worldPos);
  vec3 H = normalize(L + V);
  float ndl = max(dot(N, L), 0.0);
  float metallic = pc.materialParams.x;
  float roughness = pc.materialParams.y;
  float shininess = clamp(2.0 / pow(max(roughness, 1e-4), 4.0) - 2.0, 1.0, 1024.0);
  vec3 specColor = mix(vec3(0.04), albedo, metallic);
  float spec = pow(max(dot(N, H), 0.0), shininess);
  vec3 color = albedo * 0.15 + albedo * ndl * frame.lightColor.rgb + specColor * spec * frame.lightColor.rgb;
  outColor = vec4(color, pc.baseColorFactor.a);
}
