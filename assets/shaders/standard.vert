#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/instance.glsl"
#include "include/vertex.glsl"

bind_global_data(0) readonly uniform Global { f32_mat4 viewProjMat; };
bind_graphic_data(0) readonly buffer Mesh { VertexData[] vertices; };
bind_instance_data(0) readonly uniform Instance { InstanceData[g_maxInstances] instances; };

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  const f32_vec4 vertPos        = f32_vec4(vert_position(vertices[gl_VertexIndex]), 1.0);
  const f32_mat4 instanceMatrix = instances[gl_InstanceIndex].matrix;

  gl_Position  = viewProjMat * instanceMatrix * vertPos;
  out_texcoord = vert_texcoord(vertices[gl_VertexIndex]);
}
