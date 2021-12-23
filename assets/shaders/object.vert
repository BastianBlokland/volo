#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/mesh.glsl"
#include "include/types.glsl"

layout(location = 0) out f32_vec2 outTexcoord;

void main() {
  gl_Position = f32_vec4(mesh_position(gl_VertexIndex), 1.0);
  outTexcoord = mesh_texcoord(gl_VertexIndex);
}
