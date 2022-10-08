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

    data_reg_enum_t(g_dataReg, AssetTextureChannels);
    data_reg_const_t(g_dataReg, AssetTextureChannels, One);
    data_reg_const_t(g_dataReg, AssetTextureChannels, Four);

    data_reg_enum_t(g_dataReg, AssetTextureType);
    data_reg_const_t(g_dataReg, AssetTextureType, Byte);
    data_reg_const_t(g_dataReg, AssetTextureType, Float);

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

  PtxError_Count,
} PtxError;

static String ptx_error_str(const PtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Ptx specifies a non power-of-two texture size"),
      string_static("Ptx specifies a texture size larger then is supported"),
  };
  ASSERT(array_elems(g_msgs) == PtxError_Count, "Incorrect number of ptx-error messages");
  return g_msgs[err];
}

static f32 ptx_sample_noise_perlin(const PtxDef* def, const u32 x, const u32 y) {
  const f32 scaledX = x * def->frequency / def->size;
  const f32 scaledY = y * def->frequency / def->size;
  const f32 raw     = noise_perlin3(scaledX, scaledY, def->seed);
  const f32 norm    = raw * 0.5f + 0.5f; // Convert to a 0 - 1 range.
  return math_pow_f32(norm, def->power);
}

static f32 ptx_sample_checker(const PtxDef* def, const u32 x, const u32 y) {
  const u32 scaleDiv = math_max(def->size / 2, 1);
  const u32 scaledX  = (u32)(x * def->frequency / scaleDiv);
  const u32 scaledY  = (u32)(y * def->frequency / scaleDiv);
  return ((scaledX & 1) != (scaledY & 1)) ? 1.0f : 0.0f;
}

static f32 ptx_sample_circle(const PtxDef* def, const u32 x, const u32 y) {
  const f32 size         = def->size / def->frequency;
  const f32 radius       = size * 0.5f;
  const f32 toCenterX    = radius - math_mod_f32(x + 0.5f, size),
            toCenterY    = radius - math_mod_f32(y + 0.5f, size);
  const f32 toCenterDist = math_sqrt_f32(toCenterX * toCenterX + toCenterY * toCenterY);
  if (toCenterDist > radius) {
    return 0.0f; // Outside the circle.
  }
  return math_pow_f32(1.0f - toCenterDist / radius, def->power);
}

static f32 ptx_sample_noise_white(const PtxDef* def, Rng* rng) {
  const f32 raw = rng_sample_f32(rng);
  return math_pow_f32(raw, def->power);
}

static f32 ptx_sample_noise_white_gauss(const PtxDef* def, Rng* rng) {
  const f32 raw = rng_sample_gauss_f32(rng).a;
  return math_pow_f32(raw, def->power);
}

/**
 * Sample the procedure at a specific coordinate.
 * Returns a value in the 0-1 range.
 */
static f32 ptx_sample(const PtxDef* def, const u32 x, const u32 y, Rng* rng) {
  switch (def->type) {
  case PtxType_Zero:
    return 0.0f;
  case PtxType_One:
    return 1.0f;
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
  }
  diag_crash();
}

static usize pme_pixel_channel_size(const PtxDef* def) {
  switch (def->pixelType) {
  case AssetTextureType_Byte:
    return sizeof(u8);
  case AssetTextureType_Float:
    return sizeof(f32);
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
      const f32 sample = ptx_sample(def, x, y, rng);

      union {
        f32 f32;
        u8  u8;
      } value;
      switch (def->pixelType) {
      case AssetTextureType_Byte:
        value.u8 = (u8)(sample * 255.999f);
        break;
      case AssetTextureType_Float:
        value.f32 = sample;
        break;
      }

      const Mem pixelMem = mem_create(&pixels[(y * size + x) * pixelDataSize], pixelDataSize);
      mem_splat(pixelMem, mem_create(&value, pixelChannelSize));
    }
  }
  rng_destroy(rng);

  *outTexture = (AssetTextureComp){
      .type      = def->pixelType,
      .channels  = def->channels,
      .flags     = def->mipmaps ? AssetTextureFlags_MipMaps : 0,
      .pixelsRaw = pixels,
      .width     = size,
      .height    = size,
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
