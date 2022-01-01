#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"
#include "include/instance.glsl"
#include "include/quat.glsl"
#include "include/vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexData[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { InstanceData[c_maxInstances] u_instances; };

bind_internal(0) out f32_vec3 out_normal;
bind_internal(1) out f32_vec2 out_texcoord;

void main() {
  const f32_vec3 vertPos      = vert_position(u_vertices[gl_VertexIndex]);
  const f32_vec3 vertNormal   = vert_normal(u_vertices[gl_VertexIndex]);
  const f32_vec3 instancePos  = u_instances[gl_InstanceIndex].position.xyz;
  const f32_vec4 instanceQuat = u_instances[gl_InstanceIndex].rotation;
  const f32_vec3 worldPos     = quat_rotate(instanceQuat, vertPos) + instancePos;
  const f32_vec3 worldNormal  = quat_rotate(instanceQuat, vertNormal);

  gl_Position  = u_global.viewProj * f32_vec4(worldPos, 1);
  out_normal   = worldNormal;
  out_texcoord = vert_texcoord(u_vertices[gl_VertexIndex]);
}
