#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"
#include "include/instance.glsl"
#include "include/quat.glsl"
#include "include/vertex.glsl"

bind_spec(0) const f32 s_scale   = 1.0;
bind_spec(1) const f32 s_offsetX = 0.0;
bind_spec(2) const f32 s_offsetY = 0.0;
bind_spec(3) const f32 s_offsetZ = 0.0;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { InstanceData[c_maxInstances] u_instances; };

bind_internal(0) out f32_vec3 out_normal;
bind_internal(1) out f32_vec2 out_texcoord;

void main() {
  const Vertex vert = vert_unpack(u_vertices[gl_VertexIndex]);

  const f32_vec3 offset  = f32_vec3(s_offsetX, s_offsetY, s_offsetZ);
  const f32_vec3 meshPos = vert.position * s_scale + offset;

  const f32_vec3 instancePos  = u_instances[gl_InstanceIndex].position.xyz;
  const f32_vec4 instanceQuat = u_instances[gl_InstanceIndex].rotation;

  const f32_vec3 worldPos    = quat_rotate(instanceQuat, meshPos) + instancePos;
  const f32_vec3 worldNormal = quat_rotate(instanceQuat, vert.normal);

  gl_Position  = u_global.viewProj * f32_vec4(worldPos, 1);
  out_normal   = worldNormal;
  out_texcoord = vert.texcoord;
}
