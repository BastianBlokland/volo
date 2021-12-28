#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/instance.glsl"
#include "include/quat.glsl"
#include "include/vertex.glsl"

bind_global_data(0) readonly uniform Global { f32_mat4 viewProjMat; };
bind_graphic_data(0) readonly buffer Mesh { VertexData[] vertices; };
bind_instance_data(0) readonly uniform Instance { InstanceData[g_maxInstances] instances; };

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  const f32_vec3 vertPos      = vert_position(vertices[gl_VertexIndex]);
  const f32_vec3 instancePos  = instances[gl_InstanceIndex].position.xyz;
  const f32_vec4 instanceQuat = instances[gl_InstanceIndex].rotation;
  const f32_vec3 worldPos     = quat_rotate(instanceQuat, vertPos) + instancePos;

  gl_Position  = viewProjMat * f32_vec4(worldPos, 1);
  out_texcoord = vert_texcoord(vertices[gl_VertexIndex]);
}
