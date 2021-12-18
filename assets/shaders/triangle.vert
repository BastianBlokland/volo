#version 450
#extension GL_GOOGLE_include_directive : enable
#include "include/vertex.glsl"

VERT_INPUT_BINDING();

layout(location = 0) out vec3 fragColor;

void main() {
  gl_Position = vec4(VERT_CURRENT().position.xyz, 1.0);
  fragColor   = vec3(VERT_CURRENT().texcoord.xy, 0);
}
