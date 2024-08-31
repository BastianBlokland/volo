#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "rend_object.h"
#include "scene_tag.h"

#include "atlas_internal.h"
#include "stamp_internal.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

typedef struct {
  VfxAtlasDrawData atlasColor, atlasNormal;
} VfxStampMetaData;

ASSERT(sizeof(VfxStampMetaData) == 32, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  f32 data1[4]; // xyz: position, w: 16b flags, 16b excludeTags.
  f16 data2[4]; // xyzw: rotation quaternion.
  f16 data3[4]; // xyz: scale, w: roughness.
  f16 data4[4]; // x: atlasColorIndex, y: atlasNormalIndex, z: alphaBegin, w: alphaEnd.
  f16 data5[4]; // xy: warpScale, z: texOffsetY, w: texScaleY.
  union {
    f16 warpPoints[4][2];
    u64 warpPointData[2];
  };
} VfxStampData;

ASSERT(sizeof(VfxStampData) == 64, "Size needs to match the size defined in glsl");

void vfx_stamp_init(
    RendDrawComp* draw, const AssetAtlasComp* atlasColor, const AssetAtlasComp* atlasNormal) {
  *rend_draw_set_data_t(draw, VfxStampMetaData) = (VfxStampMetaData){
      .atlasColor  = vfx_atlas_draw_data(atlasColor),
      .atlasNormal = vfx_atlas_draw_data(atlasNormal),
  };
}

void vfx_stamp_output(RendDrawComp* draw, const VfxStamp* params) {
  const GeoVector stampSize = geo_vector(params->width, params->height, params->thickness);
  const GeoVector warpScale = geo_vector(params->warpScale.x, params->warpScale.y, 1);

  const GeoBox box = geo_box_from_center(params->pos, geo_vector_mul_comps(stampSize, warpScale));
  const GeoBox bounds = geo_box_from_rotated(&box, params->rot);

  VfxStampData* out = rend_draw_add_instance_t(draw, VfxStampData, SceneTags_Vfx, bounds);
  out->data1[0]     = params->pos.x;
  out->data1[1]     = params->pos.y;
  out->data1[2]     = params->pos.z;
  out->data1[3]     = bits_u32_as_f32((u32)params->flags | ((u32)params->excludeTags << 16));

  geo_quat_pack_f16(params->rot, out->data2);

  geo_vector_pack_f16(
      geo_vector(stampSize.x, stampSize.y, stampSize.z, params->roughness), out->data3);

  diag_assert_msg(params->atlasColorIndex <= 1024, "Index not representable by 16 bit float");
  diag_assert_msg(params->atlasNormalIndex <= 1024, "Index not representable by 16 bit float");

  geo_vector_pack_f16(
      geo_vector(
          (f32)params->atlasColorIndex,
          (f32)params->atlasNormalIndex,
          params->alphaBegin,
          params->alphaEnd),
      out->data4);

  geo_vector_pack_f16(
      geo_vector(warpScale.x, warpScale.y, params->texOffsetY, params->texScaleY), out->data5);

#ifdef VOLO_SIMD
  /**
   * Warp-points are represented by 8 floats, pack them to 16 bits in two steps.
   */
  const SimdVec warpPointsA = simd_vec_load((const f32*)&params->warpPoints[0]);
  const SimdVec warpPointsB = simd_vec_load((const f32*)&params->warpPoints[2]);
  SimdVec       warpPointsPackedA, warpPointsPackedB;
  if (g_f16cSupport) {
    COMPILER_BARRIER(); // Don't allow re-ordering 'simd_vec_f32_to_f16' before the check.
    warpPointsPackedA = simd_vec_f32_to_f16(warpPointsA);
    warpPointsPackedB = simd_vec_f32_to_f16(warpPointsB);
  } else {
    warpPointsPackedA = simd_vec_f32_to_f16_soft(warpPointsA);
    warpPointsPackedB = simd_vec_f32_to_f16_soft(warpPointsB);
  }
  out->warpPointData[0] = simd_vec_u64(warpPointsPackedA);
  out->warpPointData[1] = simd_vec_u64(warpPointsPackedB);
#else
  for (u32 i = 0; i != 4; ++i) {
    out->warpPoints[i][0] = float_f32_to_f16(params->warpPoints[i].x);
    out->warpPoints[i][1] = float_f32_to_f16(params->warpPoints[i].y);
  }
#endif
}
