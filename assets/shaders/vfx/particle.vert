#include "atlas.glsl"
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

const u32 c_flagGeometryFade      = 1 << 0;
const u32 c_flagBillboardSphere   = 1 << 1;
const u32 c_flagBillboardCylinder = 1 << 2;
const u32 c_flagBillboard         = c_flagBillboardSphere | c_flagBillboardCylinder;
const u32 c_flagShadowCaster      = 1 << 3;

struct MetaData {
  AtlasMeta atlas;
};

struct ParticleData {
  f32v4 data1; // x, y, z: position, w: atlasIndex
  f16v4 data2; // x, y, z, w: rotation quaternion
  f16v4 data3; // x, y: scale, z: opacity, w: flags
  f16v4 data4; // x, y, z, w: color
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { ParticleData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v4 out_color;
bind_internal(1) out flat f32 out_opacity;
bind_internal(2) out flat u32 out_flags;
bind_internal(3) out f32v2 out_texcoord;
bind_internal(4) out f32v3 out_worldPosition;

f32v3 ref_right(const u32 flags) {
  if ((flags & c_flagBillboard) != 0) {
    return quat_rotate(u_global.camRotation, f32v3(1, 0, 0));
  }
  return f32v3(1, 0, 0);
}

f32v3 ref_up(const u32 flags) {
  if ((flags & c_flagBillboardSphere) != 0) {
    return quat_rotate(u_global.camRotation, f32v3(0, 1, 0));
  }
  return f32v3(0, 1, 0);
}

f32v3 ref_forward() { return f32v3(0, 0, 1); }

f32v3 world_pos(const f32v3 pos, const f32v4 rotQuat, const f32v2 scale, const u32 flags) {
  const f32v2 vScaled = c_unitPositions[in_vertexIndex] * scale;
  const f32v3 vRot    = quat_rotate(rotQuat, f32v3(vScaled, 0));
  const f32v3 vFacing = ref_right(flags) * vRot.x + ref_up(flags) * vRot.y + ref_forward() * vRot.z;
  return vFacing + pos;
}

void main() {
  const f32v3 instancePos        = u_instances[in_instanceIndex].data1.xyz;
  const f32   instanceAtlasIndex = u_instances[in_instanceIndex].data1.w;
  const f32v4 instanceRotQuat    = f32v4(u_instances[in_instanceIndex].data2);
  const f32v2 instanceScale      = f32v4(u_instances[in_instanceIndex].data3).xy;
  const f32   instanceOpacity    = f32(u_instances[in_instanceIndex].data3.z);
  const u32   instanceFlags      = u32(f32(u_instances[in_instanceIndex].data3.w));
  const f32v4 instanceColor      = f32v4(u_instances[in_instanceIndex].data4);

  const f32v3 worldPos  = world_pos(instancePos, instanceRotQuat, instanceScale, instanceFlags);
  const f32v2 texOrigin = atlas_entry_origin(u_meta.atlas, instanceAtlasIndex);

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_worldPosition  = worldPos;
  out_color          = instanceColor;
  out_opacity        = instanceOpacity;
  out_flags          = instanceFlags;
  out_texcoord       = texOrigin + c_unitTexCoords[in_vertexIndex] * atlas_entry_size(u_meta.atlas);
}
