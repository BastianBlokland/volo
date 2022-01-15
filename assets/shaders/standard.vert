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
bind_internal(1) out f32_vec4 out_tangent;
bind_internal(2) out f32_vec2 out_texcoord;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32_vec3 offset  = f32_vec3(s_offsetX, s_offsetY, s_offsetZ);
  const f32_vec3 meshPos = vert.position * s_scale + offset;

  const f32_vec3 instancePos  = u_instances[in_instanceIndex].position.xyz;
  const f32_vec4 instanceQuat = u_instances[in_instanceIndex].rotation;

  const f32_vec3 worldPos = quat_rotate(instanceQuat, meshPos) + instancePos;

  out_vertexPosition = u_global.viewProj * f32_vec4(worldPos, 1);
  out_normal         = quat_rotate(instanceQuat, vert.normal);
  out_tangent        = f32_vec4(quat_rotate(instanceQuat, vert.tangent.xyz), vert.tangent.w);
  out_texcoord       = vert.texcoord;
}
