#include "atlas.glsl"
#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

#define stamp_size_max 10.0

struct MetaData {
  AtlasMeta atlasColor, atlasNormal, atlasEmissive;
};

struct StampData {
  f32v4 data1; // x, y, z: position, w: flags & excludeTags.
  f16v4 data2; // x, y, z, w: rotation quaternion.
  u16v4 data3; // x, y, z: stampScale / stamp_size_max, w: roughness & metalness.
  u16v4 data4; // x: atlasColorIdx & unused.
               // y: atlasNrmIdx & unused,
               // z: atlasEmissiveIdx & emissive.
               // w: alphaBegin & alphaEnd.
  f16v4 data5; // x, y: warpScale, z: texOffsetY, w: texScaleY.
  f16v4 data6; // x, y: warpP0 (bottom left), z, w: warpP1 (bottom right).
  f16v4 data7; // x, y: warpP2 (top left),    z, w: warpP3 (top right).
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { StampData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_position;          // World-space.
bind_internal(1) out flat f32v4 out_rotation;          // World-space.
bind_internal(2) out flat f32v3 out_scale;             // World-space.
bind_internal(3) out flat f32v3 out_atlasColorMeta;    // xy: origin, z: scale.
bind_internal(4) out flat f32v3 out_atlasNormalMeta;   // xy: origin, z: scale.
bind_internal(5) out flat f32v3 out_atlasEmissiveMeta; // xy: origin, z: scale.
bind_internal(6) out flat u32 out_flags;
bind_internal(7) out flat f32v2 out_attribute; // x: roughness, y: metalness.
bind_internal(8) out flat f32 out_emissive;
bind_internal(9) out flat f32v2 out_alpha;     // x: alphaBegin, y: alphaEnd.
bind_internal(10) out flat u32 out_excludeTags;
bind_internal(11) out flat f32v4 out_texTransform; // xy: offset, zw: scale.
bind_internal(12) out flat f32v4 out_warpP01;      // bottom left and bottom right.
bind_internal(13) out flat f32v4 out_warpP23;      // top left and top right.

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v4 instanceData1 = u_instances[in_instanceIndex].data1;
  const f32v4 instanceData2 = f32v4(u_instances[in_instanceIndex].data2);
  const u32v4 instanceData3 = u32v4(u_instances[in_instanceIndex].data3);
  const u32v4 instanceData4 = u32v4(u_instances[in_instanceIndex].data4);
  const f32v4 instanceData5 = f32v4(u_instances[in_instanceIndex].data5);
  const f32v4 instanceData6 = f32v4(u_instances[in_instanceIndex].data6);
  const f32v4 instanceData7 = f32v4(u_instances[in_instanceIndex].data7);

  const f32v3 instancePos              = instanceData1.xyz;
  const u32   instanceFlags            = floatBitsToUint(instanceData1.w) & 0xFFFF;
  const u32   instanceExcludeTags      = (floatBitsToUint(instanceData1.w) >> 16) & 0xFFFF;
  const f32v4 instanceQuat             = instanceData2;
  const f32v3 instanceScale            = (instanceData3.xyz / f32v3(0xFFFF)) * stamp_size_max;
  const f32   instanceRoughness        = f32(instanceData3.w & 0xFF) / f32(0xFF);
  const f32   instanceMetalness        = f32((instanceData3.w >> 8) & 0xFF) / f32(0xFF);
  const f32   instanceAtlasColorIdx    = f32(instanceData4.x & 0xFF);
  const f32   instanceAtlasNormalIdx   = f32(instanceData4.y & 0xFF);
  const f32   instanceAtlasEmissiveIdx = f32(instanceData4.z & 0xFF);
  const f32   instanceEmissive         = f32((instanceData4.z >> 8) & 0xFF) / f32(0xFF);
  const f32   instanceAlphaBegin       = f32(instanceData4.w & 0xFF) / f32(0xFF);
  const f32   instanceAlphaEnd         = f32((instanceData4.w >> 8) & 0xFF) / f32(0xFF);
  const f32v2 instanceWarpScale        = instanceData5.xy;
  const f32v2 instanceTexTransformY    = instanceData5.zw;
  const f32v4 instanceWarpP01          = instanceData6;
  const f32v4 instanceWarpP23          = instanceData7;

  const f32v3 boxSize        = f32v3(instanceScale.xy * instanceWarpScale, instanceScale.z);
  const f32v3 worldPos       = quat_rotate(instanceQuat, vert.position * boxSize) + instancePos;
  const f32v2 texOrgColor    = atlas_entry_origin(u_meta.atlasColor, instanceAtlasColorIdx);
  const f32v2 texOrgNormal   = atlas_entry_origin(u_meta.atlasNormal, instanceAtlasNormalIdx);
  const f32v2 texOrgEmissive = atlas_entry_origin(u_meta.atlasEmissive, instanceAtlasEmissiveIdx);

  out_vertexPosition    = u_global.viewProj * f32v4(worldPos, 1);
  out_position          = instancePos;
  out_rotation          = instanceQuat;
  out_scale             = instanceScale;
  out_atlasColorMeta    = f32v3(texOrgColor, atlas_entry_size(u_meta.atlasColor));
  out_atlasNormalMeta   = f32v3(texOrgNormal, atlas_entry_size(u_meta.atlasNormal));
  out_atlasEmissiveMeta = f32v3(texOrgEmissive, atlas_entry_size(u_meta.atlasEmissive));
  out_flags             = instanceFlags;
  out_attribute         = f32v2(instanceRoughness, instanceMetalness);
  out_emissive          = instanceEmissive;
  out_alpha             = f32v2(instanceAlphaBegin, instanceAlphaEnd);
  out_excludeTags       = instanceExcludeTags;
  out_texTransform      = f32v4(0, instanceTexTransformY.x, 1, instanceTexTransformY.y);
  out_warpP01           = instanceWarpP01;
  out_warpP23           = instanceWarpP23;
}
