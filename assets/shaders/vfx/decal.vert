#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

struct MetaData {
  f32 atlasEntriesPerDim;
  f32 atlasEntrySize;             // 1.0 / atlasEntriesPerDim
  f32 atlasEntrySizeMinusPadding; // 1.0 / atlasEntriesPerDim - atlasEntryPadding * 2.
  f32 atlasEntryPadding;
};

struct DecalData {
  f32v4 pos;   // x, y, z: position
  f32v4 rot;   // x, y, z, w: rotation quaternion
  f32v4 scale; // x, y, z: scale
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { DecalData[c_maxInstances] u_instances; };

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos   = u_instances[in_instanceIndex].pos.xyz;
  const f32v4 instanceQuat  = u_instances[in_instanceIndex].rot;
  const f32v3 instanceScale = u_instances[in_instanceIndex].scale.xyz;

  const f32v3 worldPos = quat_rotate(instanceQuat, vert.position * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
}
