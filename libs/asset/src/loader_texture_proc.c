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
#include "data_schema.h"
#include "ecs_world.h"
#include "geo_vector.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Procedurally generated textures.
 */

#define proctex_max_size (1024 * 16)

static DataReg* g_dataReg;
static DataMeta g_dataProcTexDefMeta;

typedef enum {
  ProcTexType_One,
  ProcTexType_Zero,
  ProcTexType_Checker,
  ProcTexType_Circle,
  ProcTexType_NoisePerlin,
  ProcTexType_NoiseWhite,
  ProcTexType_NoiseWhiteGauss,
  ProcTexType_BrdfIntegration, // Bidirectional reflectance distribution function.
} ProcTexType;

typedef struct {
  ProcTexType          type;
  AssetTextureType     pixelType;
  AssetTextureChannels channels;
  bool                 mipmaps;
  bool                 uncompressed;
  u32                  size;
  f32                  frequency, power;
  u32                  seed;
} ProcTexDef;

static void proctex_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_enum_t(reg, ProcTexType);
    data_reg_const_t(reg, ProcTexType, One);
    data_reg_const_t(reg, ProcTexType, Zero);
    data_reg_const_t(reg, ProcTexType, Checker);
    data_reg_const_t(reg, ProcTexType, Circle);
    data_reg_const_t(reg, ProcTexType, NoisePerlin);
    data_reg_const_t(reg, ProcTexType, NoiseWhite);
    data_reg_const_t(reg, ProcTexType, NoiseWhiteGauss);
    data_reg_const_t(reg, ProcTexType, BrdfIntegration);

    data_reg_enum_t(reg, AssetTextureChannels);
    data_reg_const_t(reg, AssetTextureChannels, One);
    data_reg_const_t(reg, AssetTextureChannels, Four);

    data_reg_enum_t(reg, AssetTextureType);
    data_reg_const_t(reg, AssetTextureType, U8);
    data_reg_const_t(reg, AssetTextureType, U16);
    data_reg_const_t(reg, AssetTextureType, F32);

    data_reg_struct_t(reg, ProcTexDef);
    data_reg_field_t(reg, ProcTexDef, type, t_ProcTexType);
    data_reg_field_t(reg, ProcTexDef, pixelType, t_AssetTextureType, .flags = DataFlags_Opt);
    data_reg_field_t(reg, ProcTexDef, channels, t_AssetTextureChannels);
    data_reg_field_t(reg, ProcTexDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, ProcTexDef, uncompressed, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, ProcTexDef, size, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, ProcTexDef, frequency, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, ProcTexDef, power, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, ProcTexDef, seed, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    // clang-format on

    g_dataProcTexDefMeta = data_meta_t(t_ProcTexDef);
    g_dataReg            = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  ProcTexError_None = 0,
  ProcTexError_SizeNonPow2,
  ProcTexError_SizeTooBig,
  ProcTexError_TooFewChannelsForBrdfIntegration,

  ProcTexError_Count,
} ProcTexError;

static String proctex_error_str(const ProcTexError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("ProcTex specifies a non power-of-two texture size"),
      string_static("ProcTex specifies a texture size larger then is supported"),
      string_static("Brdf integration requires at least two output channels"),
  };
  ASSERT(array_elems(g_msgs) == ProcTexError_Count, "Incorrect number of proctex-error messages");
  return g_msgs[err];
}

static GeoColor proctex_sample_noise_perlin(const ProcTexDef* def, const u32 x, const u32 y) {
  const f32 scaledX = x * def->frequency / def->size;
  const f32 scaledY = y * def->frequency / def->size;
  const f32 raw     = noise_perlin3(scaledX, scaledY, def->seed);
  const f32 norm    = raw * 0.5f + 0.5f; // Convert to a 0 - 1 range.
  const f32 val     = math_pow_f32(norm, def->power);
  return geo_color(val, val, val, val);
}

static GeoColor proctex_sample_checker(const ProcTexDef* def, const u32 x, const u32 y) {
  const u32 scaleDiv = math_max(def->size / 2, 1);
  const u32 scaledX  = (u32)(x * def->frequency / scaleDiv);
  const u32 scaledY  = (u32)(y * def->frequency / scaleDiv);
  return ((scaledX & 1) != (scaledY & 1)) ? geo_color_white : geo_color_black;
}

static GeoColor proctex_sample_circle(const ProcTexDef* def, const u32 x, const u32 y) {
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

static GeoColor proctex_sample_noise_white(const ProcTexDef* def, Rng* rng) {
  return geo_color(
      math_pow_f32(rng_sample_f32(rng), def->power),
      math_pow_f32(rng_sample_f32(rng), def->power),
      math_pow_f32(rng_sample_f32(rng), def->power),
      math_pow_f32(rng_sample_f32(rng), def->power));
}

static GeoColor proctex_sample_noise_white_gauss(const ProcTexDef* def, Rng* rng) {
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
  const f32 k     = (roughness * roughness) * 0.5f;
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
 * R: Scale factor over the specular radiance.
 * G: Bias factor over the specular radiance.
 *
 * Based on 'Environment BRDF' from 'Real Shading in Unreal Engine 4':
 * https://www.gamedevs.org/uploads/real-shading-in-unreal-engine-4.pdf
 */
static GeoColor proctex_sample_brdf_integration(const f32 roughness, const f32 nDotV) {
  const GeoVector view = geo_vector(math_sqrt_f32(1.0f - nDotV * nDotV), 0, nDotV);

  f32 outScale = 0;
  f32 outBias  = 0;

  enum { SampleCount = 128 };
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
static GeoColor proctex_sample(const ProcTexDef* def, const u32 x, const u32 y, Rng* rng) {
  switch (def->type) {
  case ProcTexType_Zero:
    return geo_color_clear;
  case ProcTexType_One:
    return geo_color_white;
  case ProcTexType_Checker:
    return proctex_sample_checker(def, x, y);
  case ProcTexType_Circle:
    return proctex_sample_circle(def, x, y);
  case ProcTexType_NoisePerlin:
    return proctex_sample_noise_perlin(def, x, y);
  case ProcTexType_NoiseWhite:
    return proctex_sample_noise_white(def, rng);
  case ProcTexType_NoiseWhiteGauss:
    return proctex_sample_noise_white_gauss(def, rng);
  case ProcTexType_BrdfIntegration:
    return proctex_sample_brdf_integration((x + 0.5f) / def->size, (y + 0.5f) / def->size);
  }
  diag_crash();
}

static usize proctex_pixel_channel_size(const ProcTexDef* def) {
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

static bool proctex_pixel_has_alpha(const ProcTexDef* def) {
  if (def->channels != 4) {
    return false;
  }
  switch (def->type) {
  case ProcTexType_Zero:
  case ProcTexType_Circle:
  case ProcTexType_NoisePerlin:
  case ProcTexType_NoiseWhite:
  case ProcTexType_NoiseWhiteGauss:
    return true;
  case ProcTexType_One:
  case ProcTexType_Checker:
  case ProcTexType_BrdfIntegration:
    return false;
  }
  diag_crash();
}

static void proctex_generate(const ProcTexDef* def, AssetTextureComp* outTexture) {
  const u32   size             = def->size;
  const usize pixelChannelSize = proctex_pixel_channel_size(def);
  const usize pixelDataSize    = pixelChannelSize * def->channels;
  u8*         pixels = alloc_alloc(g_alloc_heap, size * size * pixelDataSize, pixelDataSize).ptr;

  Rng* rng = rng_create_xorwow(g_alloc_heap, def->seed);
  for (u32 y = 0; y != size; ++y) {
    for (u32 x = 0; x != size; ++x) {
      const GeoColor sample = proctex_sample(def, x, y, rng);

      Mem channelMem = mem_create(&pixels[(y * size + x) * pixelDataSize], pixelDataSize);
      for (AssetTextureChannels channel = 0; channel != def->channels; ++channel) {
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

  AssetTextureFlags flags = 0;
  if (def->mipmaps) {
    flags |= AssetTextureFlags_GenerateMipMaps;
  }
  if (proctex_pixel_has_alpha(def)) {
    flags |= AssetTextureFlags_Alpha;
  }
  if (def->uncompressed) {
    flags |= AssetTextureFlags_Uncompressed;
  }
  *outTexture = (AssetTextureComp){
      .type         = def->pixelType,
      .channels     = def->channels,
      .flags        = flags,
      .pixelsRaw    = pixels,
      .width        = size,
      .height       = size,
      .layers       = 1,
      .srcMipLevels = 1,
  };
}

void asset_load_proctex(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  proctex_datareg_init();

  String         errMsg;
  ProcTexDef     def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataProcTexDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = proctex_error_str(ProcTexError_SizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.size > proctex_max_size)) {
    errMsg = proctex_error_str(ProcTexError_SizeTooBig);
    goto Error;
  }
  if (UNLIKELY(def.type == ProcTexType_BrdfIntegration && def.channels < 2)) {
    errMsg = proctex_error_str(ProcTexError_TooFewChannelsForBrdfIntegration);
    goto Error;
  }

  AssetTextureComp texture;
  proctex_generate(&def, &texture);

  *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load proc texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataProcTexDefMeta, mem_var(def));
  asset_repo_source_close(src);
}

void asset_texture_proc_jsonschema_write(DynString* str) {
  proctex_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataProcTexDefMeta, schemaFlags);
}
