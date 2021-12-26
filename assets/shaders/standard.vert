#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/vertex.glsl"

bind_global_align(0) readonly uniform Global { f32_mat4 viewProjMat; };
bind_graphic_align(0) readonly buffer Mesh { VertexPacked[] vertices; };

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  const f32_vec4 vertPos = f32_vec4(vert_position(vertices[gl_VertexIndex]), 1.0);
  gl_Position            = viewProjMat * vertPos;
  out_texcoord           = vert_texcoord(vertices[gl_VertexIndex]);
}
