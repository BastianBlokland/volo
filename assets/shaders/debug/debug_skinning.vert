#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "global.glsl"
#include "hash.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

const f32 c_alpha = 0.5;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_dynamic_data(0) readonly buffer MeshSkinned { VertexSkinnedPacked[] u_vertices; };
bind_instance_data(0) readonly uniform InstanceSkinned {
  InstanceSkinnedData[c_maxInstances] u_instances;
};

f32v4 joint_color(const u32 jointIndex) {
  return f32v4(color_from_hsv(hash_u32(jointIndex) / 4294967295.0, 1, 1), c_alpha);
}

f32v4 vertex_color(const u32v4 jointIndices, const f32v4 jointWeights) {
  return jointWeights.x * joint_color(jointIndices.x) +
         jointWeights.y * joint_color(jointIndices.y) +
         jointWeights.z * joint_color(jointIndices.z) +
         jointWeights.w * joint_color(jointIndices.w);
}

bind_internal(0) out f32v4 out_color;

void main() {
  const VertexSkinned vert = vert_skinned_unpack(u_vertices[in_vertexIndex]);

  const f32v3   instancePos   = u_instances[in_instanceIndex].posAndScale.xyz;
  const f32     instanceScale = u_instances[in_instanceIndex].posAndScale.w;
  const f32v4   instanceQuat  = u_instances[in_instanceIndex].rot;
  const f32m4x3 instanceSkinMat =
      instance_skin_mat(u_instances[in_instanceIndex], vert.jointIndices, vert.jointWeights);

  const f32v3 skinnedVertPos = instanceSkinMat * f32v4(vert.position, 1);
  const f32v3 worldPos = quat_rotate(instanceQuat, skinnedVertPos * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_color          = vertex_color(vert.jointIndices, vert.jointWeights);
}
