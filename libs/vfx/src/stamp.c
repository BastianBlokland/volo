#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "geo_box.h"
#include "rend_object.h"
#include "scene_tag.h"

#include "atlas_internal.h"
#include "stamp_internal.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

/**
 * Maximum supported stamp size (in meters).
 * NOTE: Needs to match the size defined in glsl.
 */
#define vfx_stamp_size_max 10.0f

typedef struct {
  VfxAtlasDrawData atlasColor, atlasNormal, atlasEmissive;
} VfxStampMetaData;

ASSERT(sizeof(VfxStampMetaData) == 48, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  f32 data1[4]; // xyz: position, w: 16b flags, 16b excludeTags.
  f16 data2[4]; // xyzw: rotation quaternion.
  u16 data3[4]; // xyz: scale / vfx_stamp_size_max, w: roughness & metalness.
  u16 data4[4]; // x: atlasColorIdx, y: atlasNrmIdx, z: atlasEmissiveIdx, w: alphaBegin & alphaEnd.
  f16 data5[4]; // xy: warpScale, z: texOffsetY, w: texScaleY.
  union {
    f16 warpPoints[4][2];
    u64 warpPointData[2];
  };
} VfxStampData;

ASSERT(sizeof(VfxStampData) == 64, "Size needs to match the size defined in glsl");

static u8 vfx_frac_u8(const f32 val) {
  if (val <= 0.0f) {
    return 0;
  }
  if (val >= 1.0f) {
    return u8_max;
  }
  return (u8)(val * 255.999f);
}

static u16 vfx_frac_u16(const f32 val) {
  if (val <= 0.0f) {
    return 0;
  }
  if (val >= 1.0f) {
    return u16_max;
  }
  return (u16)(val * 65535.999f);
}

static u16 vfx_combine_u16(const u8 a, const u8 b) { return (u16)a | ((u16)b << 8); }

void vfx_stamp_init(
    RendObjectComp*       obj,
    const AssetAtlasComp* atlasColor,
    const AssetAtlasComp* atlasNormal,
    const AssetAtlasComp* atlasEmissive) {

  *rend_object_set_data_t(obj, VfxStampMetaData) = (VfxStampMetaData){
      .atlasColor    = vfx_atlas_draw_data(atlasColor),
      .atlasNormal   = vfx_atlas_draw_data(atlasNormal),
      .atlasEmissive = vfx_atlas_draw_data(atlasEmissive),
  };
}

void vfx_stamp_output(RendObjectComp* obj, const VfxStamp* params) {
  const GeoVector stampSize = geo_vector(params->width, params->height, params->thickness);
  const GeoVector warpScale = geo_vector(params->warpScale.x, params->warpScale.y, 1);

  const GeoBox box = geo_box_from_center(params->pos, geo_vector_mul_comps(stampSize, warpScale));
  const GeoBox bounds = geo_box_from_rotated(&box, params->rot);

  VfxStampData* out = rend_object_add_instance_t(obj, VfxStampData, SceneTags_Vfx, bounds);
  out->data1[0]     = params->pos.x;
  out->data1[1]     = params->pos.y;
  out->data1[2]     = params->pos.z;
  out->data1[3]     = bits_u32_as_f32((u32)params->flags | ((u32)params->excludeTags << 16));

  geo_quat_pack_f16(params->rot, out->data2);

  static const f32 g_stampSizeMaxInv = 1.0f / vfx_stamp_size_max;
  const GeoVector  stampSizeFrac = geo_vector_clamp01(geo_vector_mul(stampSize, g_stampSizeMaxInv));

  out->data3[0] = vfx_frac_u16(stampSizeFrac.x);
  out->data3[1] = vfx_frac_u16(stampSizeFrac.y);
  out->data3[2] = vfx_frac_u16(stampSizeFrac.z);
  out->data3[3] = vfx_combine_u16(vfx_frac_u8(params->roughness), vfx_frac_u8(params->metalness));

  out->data4[0] = vfx_combine_u16(params->atlasColorIndex, 0);
  out->data4[1] = vfx_combine_u16(params->atlasNormalIndex, 0);
  out->data4[2] = vfx_combine_u16(params->atlasEmissiveIndex, vfx_frac_u8(params->emissive));
  out->data4[3] = vfx_combine_u16(vfx_frac_u8(params->alphaBegin), vfx_frac_u8(params->alphaEnd));

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
