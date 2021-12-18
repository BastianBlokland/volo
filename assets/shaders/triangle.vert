#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/types.glsl"
#include "include/vertex.glsl"

VERT_INPUT_BINDING();

layout(location = 0) out f32_vec3 fragColor;

void main() {
  gl_Position = f32_vec4(VERT_CURRENT().position.xyz, 1.0);
  fragColor   = f32_vec3(VERT_CURRENT().texcoord.xy, 0);
}
