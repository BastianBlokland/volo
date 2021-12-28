#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"
#include "include/vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData global; };
bind_graphic_data(0) readonly buffer Mesh { VertexData[] vertices; };

bind_internal(0) out f32_vec3 out_worldViewDir; // NOTE: non-normalized

void main() {
  const f32_vec2 vertPos = vert_position(vertices[gl_VertexIndex]).xy;

  gl_Position = f32_vec4(vertPos * 2, 0, 1); // Fullscreen at zero depth.

  const f32_mat4 clipToWorldSpace = inverse(global.viewProj);
  out_worldViewDir                = (clipToWorldSpace * gl_Position).xyz;
}
