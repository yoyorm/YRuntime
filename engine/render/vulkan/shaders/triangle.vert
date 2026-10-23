#version 450

layout(push_constant) uniform Push {
  mat4 mvp;
  vec4 color;
} pc;

void main() {
  vec2 positions[3] = vec2[](vec2(0.0, 0.5), vec2(-0.5, -0.5), vec2(0.5, -0.5));
  gl_Position = pc.mvp * vec4(positions[gl_VertexIndex], 0.0, 1.0);
}