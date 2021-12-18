#version 450
#extension GL_GOOGLE_include_directive : enable
#include "include/vertex.glsl"

VERT_INPUT_BINDING();

vec3 colors[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));

layout(location = 0) out vec3 fragColor;

void main() {
  gl_Position = vec4(VERT_CURRENT().pos.xyz, 1.0);
  fragColor   = colors[gl_VertexIndex];
}
