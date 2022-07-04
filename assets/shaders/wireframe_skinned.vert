#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_dynamic_data(0) readonly buffer MeshSkinned { VertexSkinnedPacked[] u_vertices; };
bind_instance_data(0) readonly uniform InstanceSkinned {
  InstanceSkinnedData[c_maxInstances] u_instances;
};

void main() {
  const VertexSkinned vert = vert_skinned_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos   = u_instances[in_instanceIndex].posAndScale.xyz;
  const f32   instanceScale = u_instances[in_instanceIndex].posAndScale.w;
  const f32v4 instanceQuat  = u_instances[in_instanceIndex].rot;
  const f32m4 instanceSkinMat =
      instance_skin_mat(u_instances[in_instanceIndex], vert.jointIndices, vert.jointWeights);

  const f32v3 skinnedVertPos = (instanceSkinMat * f32v4(vert.position, 1)).xyz;
  const f32v3 worldPos = quat_rotate(instanceQuat, skinnedVertPos * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
}
