#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer MeshSkinned { VertexSkinnedPacked[] u_vertices; };
bind_instance_data(0) readonly uniform InstanceSkinned {
  InstanceSkinnedData[c_maxInstances] u_instances;
};

bind_internal(0) out f32v3 out_worldNormal;
bind_internal(1) out f32v4 out_worldTangent;
bind_internal(2) out f32v2 out_texcoord;
bind_internal(3) out flat u32 out_tags;

void main() {
  const VertexSkinned vert = vert_skinned_unpack(u_vertices[in_vertexIndex]);

  const u32     instanceTags  = u_instances[in_instanceIndex].tagsAndPadding.x;
  const f32v3   instancePos   = u_instances[in_instanceIndex].posAndScale.xyz;
  const f32     instanceScale = u_instances[in_instanceIndex].posAndScale.w;
  const f32v4   instanceQuat  = u_instances[in_instanceIndex].rot;
  const f32m4x3 instanceSkinMat =
      instance_skin_mat(u_instances[in_instanceIndex], vert.jointIndices, vert.jointWeights);

  const f32v3 skinnedVertPos = instanceSkinMat * f32v4(vert.position, 1);
  const f32v3 skinnedNormal  = f32m3(instanceSkinMat) * vert.normal;
  const f32v4 skinnedTangent = f32v4(f32m3(instanceSkinMat) * vert.tangent.xyz, vert.tangent.w);

  const f32v3 worldPos = quat_rotate(instanceQuat, skinnedVertPos * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_worldNormal    = quat_rotate(instanceQuat, skinnedNormal);
  out_worldTangent   = f32v4(quat_rotate(instanceQuat, skinnedTangent.xyz), skinnedTangent.w);
  out_texcoord       = vert.texcoord;
  out_tags           = instanceTags;
}
