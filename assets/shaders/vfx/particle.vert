#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"

const u32   c_verticesPerParticle                  = 6;
const f32v2 c_unitPositions[c_verticesPerParticle] = {
    f32v2(-0.5, +0.5),
    f32v2(+0.5, +0.5),
    f32v2(-0.5, -0.5),
    f32v2(+0.5, +0.5),
    f32v2(+0.5, -0.5),
    f32v2(-0.5, -0.5),
};
const f32v2 c_unitTexCoords[c_verticesPerParticle] = {
    f32v2(0, 1),
    f32v2(1, 1),
    f32v2(0, 0),
    f32v2(1, 1),
    f32v2(1, 0),
    f32v2(0, 0),
};

struct MetaData {
  f32 atlasEntriesPerDim;
  f32 atlasEntrySize;             // 1.0 / atlasEntriesPerDim
  f32 atlasEntrySizeMinusPadding; // 1.0 / atlasEntriesPerDim - atlasEntryPadding * 2.
  f32 atlasEntryPadding;
};

struct ParticleData {
  f32v4 data1; // x, y, z: position, w: atlasIndex
  f16v4 data2; // x, y, z, w: rotation quaternion
  f16v4 data3; // x, y: scale
  f16v4 data4; // x, y, z, w: color
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { ParticleData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v4 out_color;
bind_internal(1) out flat f32 out_opacity;
bind_internal(2) out f32v2 out_texcoord;

/**
 * Compute the x and y position in the texture atlas based on the atlas-index.
 */
f32v2 atlas_entry_origin(
    const f32 index, const f32 entriesPerDim, const f32 entrySize, const f32 entryPadding) {
  // NOTE: '* entrySize' is equivalent to '/ entriesPerDim'.
  const f32 entryX = mod(index, entriesPerDim) * entrySize + entryPadding;
  const f32 entryY = floor(index * entrySize) * entrySize + entryPadding;
  return f32v2(entryX, entryY);
}

void main() {
  const f32v2 unitPos = c_unitPositions[in_vertexIndex];

  const f32v3 instancePos        = u_instances[in_instanceIndex].data1.xyz;
  const f32   instanceAtlasIndex = u_instances[in_instanceIndex].data1.w;
  const f32v4 instanceQuat       = f32v4(u_instances[in_instanceIndex].data2);
  const f32v2 instanceScale      = f32v4(u_instances[in_instanceIndex].data3).xy;
  const f32v4 instanceColor      = f32v4(u_instances[in_instanceIndex].data4);
  const f32   instanceOpacity    = f32(u_instances[in_instanceIndex].data3.z);

  const f32v3 worldPos = quat_rotate(instanceQuat, f32v3(unitPos * instanceScale, 0)) + instancePos;

  const f32v2 texOrigin = atlas_entry_origin(
      instanceAtlasIndex,
      u_meta.atlasEntriesPerDim,
      u_meta.atlasEntrySize,
      u_meta.atlasEntryPadding);

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_color          = instanceColor;
  out_opacity        = instanceOpacity;
  out_texcoord = texOrigin + c_unitTexCoords[in_vertexIndex] * u_meta.atlasEntrySizeMinusPadding;
}
