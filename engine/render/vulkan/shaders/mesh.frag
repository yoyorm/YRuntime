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

layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 3) uniform sampler2D emissiveTexture;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 baseColorFactor;
  vec4 emissiveNormalScale;
  vec4 materialParams;
  vec4 reserved;
} pc;

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265;

float distributionGGX(float NdotH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (kPi * d * d);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float gv = NdotV / (NdotV * (1.0 - k) + k);
  float gl = NdotL / (NdotL * (1.0 - k) + k);
  return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

void main() {
  vec4 base = texture(baseColorTexture, texCoord);
  vec3 albedo = base.rgb * pc.baseColorFactor.rgb;
  float alpha = base.a * pc.baseColorFactor.a;

  vec4 mr = texture(metallicRoughnessTexture, texCoord);
  float metallic = clamp(pc.materialParams.x * mr.b, 0.0, 1.0);
  float roughness = clamp(pc.materialParams.y * mr.g, 0.04, 1.0);

  vec3 emissive = texture(emissiveTexture, texCoord).rgb * pc.emissiveNormalScale.xyz;

  if (pc.materialParams.w > 0.5 && alpha < pc.materialParams.z) {
    discard;
  }

  vec3 N = normalize(worldNormal);
  vec3 dp1 = dFdx(worldPos);
  vec3 dp2 = dFdy(worldPos);
  vec2 duv1 = dFdx(texCoord);
  vec2 duv2 = dFdy(texCoord);

  vec3 T = dp1 * duv2.y - dp2 * duv1.y;
  if (dot(T, T) < 1e-12) {
    vec3 helper = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    T = cross(helper, N);
  }
  T = normalize(T - N * dot(N, T));
  vec3 B = cross(N, T);
  vec3 n = texture(normalTexture, texCoord).xyz * 2.0 - 1.0;
  n.xy *= pc.emissiveNormalScale.w;
  N = normalize(mat3(T, B, N) * n);

  vec3 V = normalize(frame.eye.xyz - worldPos);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  vec3 lighting = vec3(0.0);

  uint count = uint(frame.lightCount.x);
  for (uint i = 0u; i < count && i < 8u; ++i) {
    Light light = frame.lights[i];
    vec3 L;
    float attenuation = 1.0;

    if (light.positionType.w < 0.5) {
      L = normalize(-light.directionRange.xyz);
    } else {
      vec3 toLight = light.positionType.xyz - worldPos;
      float dist2 = dot(toLight, toLight);
      float dist = sqrt(dist2);
      L = toLight / max(dist, 1e-4);
      float range = max(light.directionRange.w, 1e-4);
      float ratio = dist / range;
      float falloff = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
      attenuation = (falloff * falloff) / (dist2 + 1.0);
    }

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F = fresnelSchlick(VdotH, F0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 specular = D * G * F / (4.0 * NdotL * NdotV + 1e-4);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / kPi;

    vec3 radiance = light.colorIntensity.xyz * light.colorIntensity.w;
    lighting += (diffuse + specular) * NdotL * radiance * attenuation;
  }

  vec3 color = albedo * 0.03 + emissive + lighting;
  outColor = vec4(color, alpha);
}
