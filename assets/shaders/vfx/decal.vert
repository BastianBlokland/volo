#version 450
#extension GL_GOOGLE_include_directive : enable

#include "atlas.glsl"
#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

struct MetaData {
  AtlasMeta atlasColor, atlasNormal;
};

struct DecalData {
  f32v4 data1; // x, y, z: position, w: flags
  f32v4 data2; // x, y, z, w: rotation quaternion.
  f32v4 data3; // x, y, z: scale, w: unused.
  f32v4 data4; // x: atlasColorIndex, y: atlasNormalIndex, z: roughness, w: unused.
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { DecalData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_position;        // World-space.
bind_internal(1) out flat f32v4 out_rotation;        // World-space.
bind_internal(2) out flat f32v3 out_scale;           // World-space.
bind_internal(3) out flat f32v4 out_atlasColorMeta;  // xy: origin, z: scale, w: unused.
bind_internal(4) out flat f32v4 out_atlasNormalMeta; // xy: origin, z: scale, w: unused.
bind_internal(5) out flat u32 out_flags;
bind_internal(6) out flat f32 out_roughness;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos              = u_instances[in_instanceIndex].data1.xyz;
  const f32v4 instanceQuat             = u_instances[in_instanceIndex].data2;
  const f32v3 instanceScale            = u_instances[in_instanceIndex].data3.xyz;
  const f32   instanceAtlasColorIndex  = u_instances[in_instanceIndex].data4.x;
  const f32   instanceAtlasNormalIndex = u_instances[in_instanceIndex].data4.y;
  const u32   instanceFlags            = u32(u_instances[in_instanceIndex].data1.w);
  const f32   instanceRoughness        = u_instances[in_instanceIndex].data4.z;

  const f32v3 worldPos = quat_rotate(instanceQuat, vert.position * instanceScale) + instancePos;
  const f32v2 colorTexOrigin  = atlas_entry_origin(u_meta.atlasColor, instanceAtlasColorIndex);
  const f32v2 normalTexOrigin = atlas_entry_origin(u_meta.atlasNormal, instanceAtlasNormalIndex);

  out_vertexPosition  = u_global.viewProj * f32v4(worldPos, 1);
  out_position        = instancePos;
  out_rotation        = instanceQuat;
  out_scale           = instanceScale;
  out_atlasColorMeta  = f32v4(colorTexOrigin, atlas_entry_size(u_meta.atlasColor), 0);
  out_atlasNormalMeta = f32v4(normalTexOrigin, atlas_entry_size(u_meta.atlasNormal), 0);
  out_flags           = instanceFlags;
  out_roughness       = instanceRoughness;
}
