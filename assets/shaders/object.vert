#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/types.glsl"
#include "include/vertex.glsl"

layout(set = 0, binding = 0, std140) readonly buffer VertexBuffer { Vertex[] vertices; };

layout(location = 0) out f32_vec2 outTexcoord;

Vertex vertex_current() { return vertices[gl_VertexIndex]; }

void main() {
  gl_Position = f32_vec4(vertex_current().position.xyz, 1.0);
  outTexcoord = f32_vec2(vertex_current().texcoord.xy);
}
