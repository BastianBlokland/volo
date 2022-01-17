#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"
#include "include/vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };

bind_internal(0) out f32_vec3 out_viewDir; // NOTE: non-normalized

void main() {
  const Vertex   vert             = vert_unpack(u_vertices[in_vertexIndex]);
  const f32_mat4 clipToWorldSpace = inverse(u_global.viewProj);

  out_vertexPosition = f32_vec4(vert.position * 2, 1); // Fullscreen at zero depth.
  out_viewDir        = (clipToWorldSpace * out_vertexPosition).xyz;
}
