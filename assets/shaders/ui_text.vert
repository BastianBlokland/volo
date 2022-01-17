#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/vertex.glsl"

bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  out_vertexPosition = f32_vec4(vert.position * 2, 1);
  out_texcoord       = vert.texcoord;
}
