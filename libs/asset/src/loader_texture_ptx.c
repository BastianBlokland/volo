#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_noise.h"
#include "core_rng.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "geo_vector.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * ProceduralTeXture - Procedurally generated textures.
 */

#define ptx_max_size (1024 * 16)

static DataReg* g_dataReg;
static DataMeta g_dataPtxDefMeta;

typedef enum {
  PtxType_One,
  PtxType_Zero,
  PtxType_Checker,
  PtxType_Circle,
  PtxType_NoisePerlin,
  PtxType_NoiseWhite,
  PtxType_NoiseWhiteGauss,
  PtxType_BrdfIntegration, // Bidirectional reflectance distribution function.
} PtxType;

typedef struct {
  PtxType              type;
  AssetTextureType     pixelType;
  AssetTextureChannels channels;
  bool                 mipmaps;
  u32                  size;
  f32                  frequency, power;
  u32                  seed;
} PtxDef;

static void ptx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_enum_t(g_dataReg, PtxType);
    data_reg_const_t(g_dataReg, PtxType, One);
    data_reg_const_t(g_dataReg, PtxType, Zero);
    data_reg_const_t(g_dataReg, PtxType, Checker);
    data_reg_const_t(g_dataReg, PtxType, Circle);
    data_reg_const_t(g_dataReg, PtxType, NoisePerlin);
    data_reg_const_t(g_dataReg, PtxType, NoiseWhite);
    data_reg_const_t(g_dataReg, PtxType, NoiseWhiteGauss);
    data_reg_const_t(g_dataReg, PtxType, BrdfIntegration);

    data_reg_enum_t(g_dataReg, AssetTextureChannels);
    data_reg_const_t(g_dataReg, AssetTextureChannels, One);
    data_reg_const_t(g_dataReg, AssetTextureChannels, Four);

    data_reg_enum_t(g_dataReg, AssetTextureType);
    data_reg_const_t(g_dataReg, AssetTextureType, U8);
    data_reg_const_t(g_dataReg, AssetTextureType, U16);
    data_reg_const_t(g_dataReg, AssetTextureType, F32);

    data_reg_struct_t(g_dataReg, PtxDef);
    data_reg_field_t(g_dataReg, PtxDef, type, t_PtxType);
    data_reg_field_t(g_dataReg, PtxDef, pixelType, t_AssetTextureType, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PtxDef, channels, t_AssetTextureChannels);
    data_reg_field_t(g_dataReg, PtxDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PtxDef, size, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PtxDef, frequency, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PtxDef, power, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PtxDef, seed, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    // clang-format on

    g_dataPtxDefMeta = data_meta_t(t_PtxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  PtxError_None = 0,
  PtxError_SizeNonPow2,
  PtxError_SizeTooBig,
  PtxError_TooFewChannelsForBrdfIntegration,

  PtxError_Count,
} PtxError;

static String ptx_error_str(const PtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Ptx specifies a non power-of-two texture size"),
      string_static("Ptx specifies a texture size larger then is supported"),
      string_static("Brdf integration requires at least two output channels"),
  };
  ASSERT(array_elems(g_msgs) == PtxError_Count, "Incorrect number of ptx-error messages");
  return g_msgs[err];
}

static GeoColor ptx_sample_noise_perlin(const PtxDef* def, const u32 x, const u32 y) {
  const f32 scaledX = x * def->frequency / def->size;
  const f32 scaledY = y * def->frequency / def->size;
  const f32 raw     = noise_perlin3(scaledX, scaledY, def->seed);
  const f32 norm    = raw * 0.5f + 0.5f; // Convert to a 0 - 1 range.
  const f32 val     = math_pow_f32(norm, def->power);
  return geo_color(val, val, val, val);
}

static GeoColor ptx_sample_checker(const PtxDef* def, const u32 x, const u32 y) {
  const u32 scaleDiv = math_max(def->size / 2, 1);
  const u32 scaledX  = (u32)(x * def->frequency / scaleDiv);
  const u32 scaledY  = (u32)(y * def->frequency / scaleDiv);
  return ((scaledX & 1) != (scaledY & 1)) ? geo_color_white : geo_color_black;
}

static GeoColor ptx_sample_circle(const PtxDef* def, const u32 x, const u32 y) {
  const f32 size         = def->size / def->frequency;
  const f32 radius       = size * 0.5f;
  const f32 toCenterX    = radius - math_mod_f32(x + 0.5f, size),
            toCenterY    = radius - math_mod_f32(y + 0.5f, size);
  const f32 toCenterDist = math_sqrt_f32(toCenterX * toCenterX + toCenterY * toCenterY);
  if (toCenterDist > radius) {
    return geo_color_clear; // Outside the circle.
  }
  const f32 val = math_pow_f32(1.0f - toCenterDist / radius, def->power);
  return geo_color(val, val, val, val);
}

static GeoColor ptx_sample_noise_white(const PtxDef* def, Rng* rng) {
  return geo_color(
      math_pow_f32(rng_sample_f32(rng), def->power),
      math_pow_f32(rng_sample_f32(rng), def->power),
      math_pow_f32(rng_sample_f32(rng), def->power),
      math_pow_f32(rng_sample_f32(rng), def->power));
}

static GeoColor ptx_sample_noise_white_gauss(const PtxDef* def, Rng* rng) {
  return geo_color(
      math_pow_f32(rng_sample_gauss_f32(rng).a, def->power),
      math_pow_f32(rng_sample_gauss_f32(rng).a, def->power),
      math_pow_f32(rng_sample_gauss_f32(rng).a, def->power),
      math_pow_f32(rng_sample_gauss_f32(rng).a, def->power));
}

/**
 * Low-discrepancy sequence of pseudo random points on a 2d hemisphere (Hammersley sequence).
 * More information: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
 */
static GeoVector hemisphere_2d_hammersley(const u32 index, const u32 count) {
  u32 indexBits = index;
  indexBits     = (indexBits << 16) | (indexBits >> 16);
  indexBits = ((indexBits & u32_lit(0x55555555)) << 1) | ((indexBits & u32_lit(0xAAAAAAAA)) >> 1);
  indexBits = ((indexBits & u32_lit(0x33333333)) << 2) | ((indexBits & u32_lit(0xCCCCCCCC)) >> 2);
  indexBits = ((indexBits & u32_lit(0x0F0F0F0F)) << 4) | ((indexBits & u32_lit(0xF0F0F0F0)) >> 4);
  indexBits = ((indexBits & u32_lit(0x00FF00FF)) << 8) | ((indexBits & u32_lit(0xFF00FF00)) >> 8);
  const f32 radicalInverseVdc = (f32)indexBits * 2.3283064365386963e-10f; // / 0x100000000
  return geo_vector(index / (f32)count, radicalInverseVdc);
}

/**
 * Generate a sample vector in tangent space that's biased towards the normal (importance sampling).
 * Roughness controls the size of the specular lobe (smooth vs blurry reflections).
 */
static GeoVector importance_sample_ggx(const u32 index, const u32 count, const f32 roughness) {
  const GeoVector vXi      = hemisphere_2d_hammersley(index, count);
  const f32       a        = roughness * roughness;
  const f32       phi      = 2.0f * math_pi_f32 * vXi.x;
  const f32       cosTheta = math_sqrt_f32((1.0f - vXi.y) / (1.0f + (a * a - 1.0f) * vXi.y));
  const f32       sinTheta = math_sqrt_f32(1.0f - cosTheta * cosTheta);
  return geo_vector(math_cos_f32(phi) * sinTheta, math_sin_f32(phi) * sinTheta, cosTheta);
}

static f32 geometry_schlick_ggx(const f32 nDotV, const f32 roughness) {
  const f32 k     = (roughness * roughness) / 2.0f;
  const f32 nom   = nDotV;
  const f32 denom = nDotV * (1.0f - k) + k;
  return nom / denom;
}

/**
 * Statistically approximates the relative surface area where its micro surface-details overshadow
 * each other, causing light rays to be occluded.
 */
static f32 geometry_smith(const f32 nDotV, const f32 nDotL, const f32 roughness) {
  return geometry_schlick_ggx(nDotL, roughness) * geometry_schlick_ggx(nDotV, roughness);
}

/**
 * Compute a BRDF (Bidirectional reflectance distribution function) integration lookup table.
 * Based on 'Environment BRDF' from 'Real Shading in Unreal Engine 4':
 * https://www.gamedevs.org/uploads/real-shading-in-unreal-engine-4.pdf
 */
static GeoColor ptx_sample_brdf_integration(const f32 roughness, const f32 nDotV) {
  const GeoVector view = geo_vector(math_sqrt_f32(1.0f - nDotV * nDotV), 0, nDotV);

  f32 outScale = 0;
  f32 outBias  = 0;

  enum { SampleCount = 256 };
  for (u32 i = 0; i != SampleCount; ++i) {
    const GeoVector halfDir  = importance_sample_ggx(i, SampleCount, roughness);
    const f32       vDotH    = math_max(geo_vector_dot(view, halfDir), 0);
    const GeoVector lightDir = geo_vector_sub(geo_vector_mul(halfDir, vDotH * 2.0f), view);

    const f32 nDotL = math_max(lightDir.z, 0);
    const f32 nDotH = math_max(halfDir.z, 0);

    if (nDotL > 0) {
      const f32 geoFrac     = geometry_smith(nDotV, nDotL, roughness);
      const f32 geoVisFrac  = (geoFrac * vDotH) / (nDotH * nDotV);
      const f32 fresnelFrac = math_pow_f32(1.0f - vDotH, 5.0f);

      outScale += (1.0f - fresnelFrac) * geoVisFrac;
      outBias += fresnelFrac * geoVisFrac;
    }
  }

  outScale /= SampleCount;
  outBias /= SampleCount;
  return geo_color(outScale, outBias, 0, 1);
}

/**
 * Sample the procedure at a specific coordinate.
 * Returns a value in the 0-1 range.
 */
static GeoColor ptx_sample(const PtxDef* def, const u32 x, const u32 y, Rng* rng) {
  switch (def->type) {
  case PtxType_Zero:
    return geo_color_clear;
  case PtxType_One:
    return geo_color_white;
  case PtxType_Checker:
    return ptx_sample_checker(def, x, y);
  case PtxType_Circle:
    return ptx_sample_circle(def, x, y);
  case PtxType_NoisePerlin:
    return ptx_sample_noise_perlin(def, x, y);
  case PtxType_NoiseWhite:
    return ptx_sample_noise_white(def, rng);
  case PtxType_NoiseWhiteGauss:
    return ptx_sample_noise_white_gauss(def, rng);
  case PtxType_BrdfIntegration:
    return ptx_sample_brdf_integration((x + 0.5f) / def->size, (y + 0.5f) / def->size);
  }
  diag_crash();
}

static usize pme_pixel_channel_size(const PtxDef* def) {
  switch (def->pixelType) {
  case AssetTextureType_U8:
    return sizeof(u8);
  case AssetTextureType_U16:
    return sizeof(u16);
  case AssetTextureType_F32:
    return sizeof(f32);
  case AssetTextureType_Count:
    UNREACHABLE
  }
  diag_crash();
}

static void ptx_generate(const PtxDef* def, AssetTextureComp* outTexture) {
  const u32   size             = def->size;
  const usize pixelChannelSize = pme_pixel_channel_size(def);
  const usize pixelDataSize    = pixelChannelSize * def->channels;
  u8*         pixels = alloc_alloc(g_alloc_heap, size * size * pixelDataSize, pixelDataSize).ptr;

  Rng* rng = rng_create_xorwow(g_alloc_heap, def->seed);
  for (u32 y = 0; y != size; ++y) {
    for (u32 x = 0; x != size; ++x) {
      const GeoColor sample = ptx_sample(def, x, y, rng);

      Mem channelMem = mem_create(&pixels[(y * size + x) * pixelDataSize], pixelDataSize);
      for (u32 channel = 0; channel != def->channels; ++channel) {
        union {
          u8  u8;
          u16 u16;
          f32 f32;
        } value;
        switch (def->pixelType) {
        case AssetTextureType_U8:
          value.u8 = (u8)(sample.data[channel] * 255.999f);
          break;
        case AssetTextureType_U16:
          value.u16 = (u16)(sample.data[channel] * 65535.99f);
          break;
        case AssetTextureType_F32:
          value.f32 = sample.data[channel];
          break;
        case AssetTextureType_Count:
          UNREACHABLE
        }
        mem_cpy(channelMem, mem_create(&value, pixelChannelSize));
        channelMem = mem_consume(channelMem, pixelChannelSize);
      }
    }
  }
  rng_destroy(rng);

  *outTexture = (AssetTextureComp){
      .type         = def->pixelType,
      .channels     = def->channels,
      .flags        = def->mipmaps ? AssetTextureFlags_GenerateMipMaps : 0,
      .pixelsRaw    = pixels,
      .width        = size,
      .height       = size,
      .layers       = 1,
      .srcMipLevels = 1,
  };
}

void asset_load_ptx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ptx_datareg_init();

  String         errMsg;
  PtxDef         def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataPtxDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = ptx_error_str(PtxError_SizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.size > ptx_max_size)) {
    errMsg = ptx_error_str(PtxError_SizeTooBig);
    goto Error;
  }
  if (UNLIKELY(def.type == PtxType_BrdfIntegration && def.channels < 2)) {
    errMsg = ptx_error_str(PtxError_TooFewChannelsForBrdfIntegration);
    goto Error;
  }

  AssetTextureComp texture;
  ptx_generate(&def, &texture);

  *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load ptx texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataPtxDefMeta, mem_var(def));
  asset_repo_source_close(src);
}
