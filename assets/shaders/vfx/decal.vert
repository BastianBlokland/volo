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
  f32v4 data1; // x, y, z: position, w: 16b flags, 16b excludeTags.
  f16v4 data2; // x, y, z, w: rotation quaternion.
  f16v4 data3; // x, y, z: decalScale, w: roughness.
  f16v4 data4; // x: atlasColorIndex, y: atlasNormalIndex, z: alphaBegin, w: alphaEnd.
  f16v4 data5; // x, y: warpScale, z: texOffsetY, w: texScaleY.
  f16v4 data6; // x, y: warpP0 (bottom left), z, w: warpP1 (bottom right).
  f16v4 data7; // x, y: warpP2 (top left),    z, w: warpP3 (top right).
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { DecalData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_position;        // World-space.
bind_internal(1) out flat f32v4 out_rotation;        // World-space.
bind_internal(2) out flat f32v3 out_scale;           // World-space.
bind_internal(3) out flat f32v3 out_atlasColorMeta;  // xy: origin, z: scale.
bind_internal(4) out flat f32v3 out_atlasNormalMeta; // xy: origin, z: scale.
bind_internal(5) out flat u32 out_flags;
bind_internal(6) out flat f32 out_roughness;
bind_internal(7) out flat f32v2 out_alpha; // x: alphaBegin, y: alphaEnd.
bind_internal(8) out flat u32 out_excludeTags;
bind_internal(9) out flat f32v4 out_texTransform; // xy: offset, zw: scale.
bind_internal(10) out flat f32v4 out_warpP01;     // bottom left and bottom right.
bind_internal(11) out flat f32v4 out_warpP23;     // top left and top right.

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v4 instanceData1 = u_instances[in_instanceIndex].data1;
  const f32v4 instanceData2 = f32v4(u_instances[in_instanceIndex].data2);
  const f32v4 instanceData3 = f32v4(u_instances[in_instanceIndex].data3);
  const f32v4 instanceData4 = f32v4(u_instances[in_instanceIndex].data4);
  const f32v4 instanceData5 = f32v4(u_instances[in_instanceIndex].data5);
  const f32v4 instanceData6 = f32v4(u_instances[in_instanceIndex].data6);
  const f32v4 instanceData7 = f32v4(u_instances[in_instanceIndex].data7);

  const f32v3 instancePos              = instanceData1.xyz;
  const u32   instanceFlags            = floatBitsToUint(instanceData1.w) & 0xFFFF;
  const u32   instanceExcludeTags      = (floatBitsToUint(instanceData1.w) >> 16) & 0xFFFF;
  const f32v4 instanceQuat             = instanceData2;
  const f32v3 instanceScale            = instanceData3.xyz;
  const f32   instanceRoughness        = instanceData3.w;
  const f32   instanceAtlasColorIndex  = instanceData4.x;
  const f32   instanceAtlasNormalIndex = instanceData4.y;
  const f32v2 instanceAlpha            = instanceData4.zw;
  const f32v2 instanceWarpScale        = instanceData5.xy;
  const f32v2 instanceTexTransformY    = instanceData5.zw;
  const f32v4 instanceWarpP01          = instanceData6;
  const f32v4 instanceWarpP23          = instanceData7;

  const f32v3 boxSize         = f32v3(instanceScale.xy * instanceWarpScale, instanceScale.z);
  const f32v3 worldPos        = quat_rotate(instanceQuat, vert.position * boxSize) + instancePos;
  const f32v2 colorTexOrigin  = atlas_entry_origin(u_meta.atlasColor, instanceAtlasColorIndex);
  const f32v2 normalTexOrigin = atlas_entry_origin(u_meta.atlasNormal, instanceAtlasNormalIndex);

  out_vertexPosition  = u_global.viewProj * f32v4(worldPos, 1);
  out_position        = instancePos;
  out_rotation        = instanceQuat;
  out_scale           = instanceScale;
  out_atlasColorMeta  = f32v3(colorTexOrigin, atlas_entry_size(u_meta.atlasColor));
  out_atlasNormalMeta = f32v3(normalTexOrigin, atlas_entry_size(u_meta.atlasNormal));
  out_flags           = instanceFlags;
  out_roughness       = instanceRoughness;
  out_alpha           = instanceAlpha;
  out_excludeTags     = instanceExcludeTags;
  out_texTransform    = f32v4(0, instanceTexTransformY.x, 1, instanceTexTransformY.y);
  out_warpP01         = instanceWarpP01;
  out_warpP23         = instanceWarpP23;
}
