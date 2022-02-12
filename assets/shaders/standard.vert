#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { InstanceData[c_maxInstances] u_instances; };

bind_internal(0) out f32v3 out_worldPosition;
bind_internal(1) out f32v3 out_worldNormal;
bind_internal(2) out f32v4 out_worldTangent;
bind_internal(3) out f32v2 out_texcoord;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos   = u_instances[in_instanceIndex].posAndScale.xyz;
  const f32   instanceScale = u_instances[in_instanceIndex].posAndScale.w;
  const f32v4 instanceQuat  = u_instances[in_instanceIndex].rot;

  const f32v3 worldPos = quat_rotate(instanceQuat, vert.position * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_worldPosition  = worldPos;
  out_worldNormal    = quat_rotate(instanceQuat, vert.normal);
  out_worldTangent   = f32v4(quat_rotate(instanceQuat, vert.tangent.xyz), vert.tangent.w);
  out_texcoord       = vert.texcoord;
}
