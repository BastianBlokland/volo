#version 450
#extension GL_GOOGLE_include_directive : enable

#include "atlas.glsl"
#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

struct MetaData {
  AtlasMeta atlasColor;
};

struct DecalData {
  f32v4 data1; // x, y, z: position, w: atlasColorIndex.
  f32v4 data2; // x, y, z, w: rotation quaternion
  f32v4 data3; // x, y, z: scale
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { DecalData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_positionInv;    // -worldSpacePos.
bind_internal(1) out flat f32v4 out_rotationInv;    // inverse(worldSpaceRot).
bind_internal(2) out flat f32v3 out_scaleInv;       // 1.0 / worldSpaceScale.
bind_internal(3) out flat f32v4 out_atlasColorRect; // xy: origin, zw: scale.

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos             = u_instances[in_instanceIndex].data1.xyz;
  const f32   instanceColorAtlasIndex = u_instances[in_instanceIndex].data1.w;
  const f32v4 instanceQuat            = u_instances[in_instanceIndex].data2;
  const f32v3 instanceScale           = u_instances[in_instanceIndex].data3.xyz;

  const f32v3 worldPos = quat_rotate(instanceQuat, vert.position * instanceScale) + instancePos;
  const f32v2 colorTexOrigin = atlas_entry_origin(u_meta.atlasColor, instanceColorAtlasIndex);

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_positionInv    = -instancePos;
  out_rotationInv    = quat_inverse(instanceQuat);
  out_scaleInv       = 1.0 / instanceScale;
  out_atlasColorRect = f32v4(colorTexOrigin, atlas_entry_size(u_meta.atlasColor).xx);
}
