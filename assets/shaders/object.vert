#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/types.glsl"
#include "include/vertex.glsl"

bind_graphic_align(0) readonly buffer Mesh { VertexPacked[] vertices; };

layout(location = 0) out f32_vec2 outTexcoord;

void main() {
  gl_Position = f32_vec4(vert_position(vertices[gl_VertexIndex]), 1.0);
  outTexcoord = vert_texcoord(vertices[gl_VertexIndex]);
}
