#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "data.h"
#include "data_registry.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

/**
 * ArrayTeXture - Creates multi-layer textures by combining other textures.
 */

#define atx_max_textures 100
#define atx_max_layers 256
#define atx_max_size 2048
#define atx_max_generates_per_tick 1
#define atx_spec_irradiance_mips 5

static const GeoQuat g_cubeFaceRot[] = {
    {0, 0.7071068f, 0, 0.7071068f},  // Forward to right.
    {0, -0.7071068f, 0, 0.7071068f}, // Forward to left.
    {0.7071068f, 0, 0, 0.7071068f},  // Forward to down.
    {-0.7071068f, 0, 0, 0.7071068f}, // Forward to up.
    {0, 0, 0, 1},                    // Forward to forward.
    {0, 1, 0, 0},                    // Forward to backward.
};

static DataReg* g_dataReg;
static DataMeta g_dataAtxDefMeta;

typedef enum {
  AtxType_Array,
  AtxType_Cube,
  AtxType_CubeDiffIrradiance,
  AtxType_CubeSpecIrradiance,
} AtxType;

typedef struct {
  AtxType type;
  bool    mipmaps;
  u32     sizeX, sizeY;
  struct {
    String* values;
    usize   count;
  } textures;
} AtxDef;

static void atx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_enum_t(reg, AtxType);
    data_reg_const_t(reg, AtxType, Array);
    data_reg_const_t(reg, AtxType, Cube);
    data_reg_const_t(reg, AtxType, CubeDiffIrradiance);
    data_reg_const_t(reg, AtxType, CubeSpecIrradiance);

    data_reg_struct_t(reg, AtxDef);
    data_reg_field_t(reg, AtxDef, type, t_AtxType);
    data_reg_field_t(reg, AtxDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AtxDef, sizeX, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AtxDef, sizeY, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AtxDef, textures, data_prim_t(String), .flags = DataFlags_NotEmpty, .container = DataContainer_Array);
    // clang-format on

    g_dataAtxDefMeta = data_meta_t(t_AtxDef);
    g_dataReg        = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define(AssetAtxLoadComp) {
  AtxDef   def;
  DynArray textures; // EcsEntityId[].
};

static void ecs_destruct_atx_load_comp(void* data) {
  AssetAtxLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtxDefMeta, mem_var(comp->def));
  dynarray_destroy(&comp->textures);
}

typedef enum {
  AtxError_None = 0,
  AtxError_NoTextures,
  AtxError_TooManyTextures,
  AtxError_TooManyLayers,
  AtxError_SizeTooBig,
  AtxError_InvalidTexture,
  AtxError_MismatchType,
  AtxError_MismatchChannels,
  AtxError_MismatchEncoding,
  AtxError_MismatchSize,
  AtxError_InvalidCubeAspect,
  AtxError_UnsupportedInputTypeForResampling,
  AtxError_InvalidCubeTextureCount,
  AtxError_InvalidCubeIrradianceInputType,

  AtxError_Count,
} AtxError;

static String atx_error_str(const AtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Atx does not specify any textures"),
      string_static("Atx specifies more textures then are supported"),
      string_static("Atx specifies more layers then are supported"),
      string_static("Atx specifies a size larger then is supported"),
      string_static("Atx specifies an invalid texture"),
      string_static("Atx textures have different types"),
      string_static("Atx textures have different channel counts"),
      string_static("Atx textures have different encodings"),
      string_static("Atx textures have different sizes"),
      string_static("Atx cube / cube-irradiance needs to be square"),
      string_static("Atx resampling is only supported for rgba 8bit input textures"),
      string_static("Atx cube / cube-irradiance needs 6 textures"),
      string_static("Atx cube-irradiance needs rgba 8bit input textures"),
  };
  ASSERT(array_elems(g_msgs) == AtxError_Count, "Incorrect number of atx-error messages");
  return g_msgs[err];
}

static AssetTextureFlags atx_texture_flags(const AtxDef* def, const bool srgb) {
  AssetTextureFlags flags = 0;
  switch (def->type) {
  case AtxType_Array:
    break;
  case AtxType_Cube:
  case AtxType_CubeDiffIrradiance:
  case AtxType_CubeSpecIrradiance:
    flags |= AssetTextureFlags_CubeMap;
    break;
  }
  if (def->mipmaps) {
    flags |= AssetTextureFlags_GenerateMipMaps;
  }
  if (srgb) {
    flags |= AssetTextureFlags_Srgb;
  }
  return flags;
}

static bool atx_output_cube(const AtxDef* def) {
  switch (def->type) {
  case AtxType_Array:
    return false;
  case AtxType_Cube:
  case AtxType_CubeDiffIrradiance:
  case AtxType_CubeSpecIrradiance:
    return true;
  }
  diag_crash();
}

static u32 atx_output_mips(const AtxDef* def) {
  switch (def->type) {
  case AtxType_Array:
  case AtxType_Cube:
  case AtxType_CubeDiffIrradiance:
    return 1;
  case AtxType_CubeSpecIrradiance:
    return atx_spec_irradiance_mips;
  }
  diag_crash();
}

static bool atx_output_irradiance(const AtxDef* def) {
  switch (def->type) {
  case AtxType_Array:
  case AtxType_Cube:
    return false;
  case AtxType_CubeDiffIrradiance:
  case AtxType_CubeSpecIrradiance:
    return true;
  }
  diag_crash();
}

struct AtxCubePoint {
  u32 face;
  f32 coordX, coordY;
};

static struct AtxCubePoint atx_cube_lookup(const GeoVector dir) {
  struct AtxCubePoint res;
  float               scale;
  const GeoVector     dirAbs = geo_vector_abs(dir);
  if (dirAbs.z >= dirAbs.x && dirAbs.z >= dirAbs.y) {
    res.face   = dir.z < 0.0f ? 5 : 4;
    scale      = 0.5f / dirAbs.z;
    res.coordX = dir.z < 0.0f ? -dir.x : dir.x;
    res.coordY = dir.y;
  } else if (dirAbs.y >= dirAbs.x) {
    res.face   = dir.y < 0.0f ? 2 : 3;
    scale      = 0.5f / dirAbs.y;
    res.coordX = dir.x;
    res.coordY = dir.y < 0.0f ? dir.z : -dir.z;
  } else {
    res.face   = dir.x < 0.0f ? 1 : 0;
    scale      = 0.5f / dirAbs.x;
    res.coordX = dir.x < 0.0 ? dir.z : -dir.z;
    res.coordY = dir.y;
  }
  res.coordX = res.coordX * scale + 0.5f;
  res.coordY = res.coordY * scale + 0.5f;
  return res;
}

static AssetTexturePixelB4 atx_color_to_b4_linear(const GeoColor color) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;
  return (AssetTexturePixelB4){
      .r = (u8)(color.r * g_u8MaxPlusOneRoundDown),
      .g = (u8)(color.g * g_u8MaxPlusOneRoundDown),
      .b = (u8)(color.b * g_u8MaxPlusOneRoundDown),
      .a = (u8)(color.a * g_u8MaxPlusOneRoundDown),
  };
}

static AssetTexturePixelB4 atx_color_to_b4_srgb(const GeoColor color) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;
  // Simple approximation of the srgb curve: https://en.wikipedia.org/wiki/SRGB.
  static const f32 g_gammaInv = 1.0f / 2.2f;
  return (AssetTexturePixelB4){
      .r = (u8)(math_pow_f32(color.r, g_gammaInv) * g_u8MaxPlusOneRoundDown),
      .g = (u8)(math_pow_f32(color.g, g_gammaInv) * g_u8MaxPlusOneRoundDown),
      .b = (u8)(math_pow_f32(color.b, g_gammaInv) * g_u8MaxPlusOneRoundDown),
      .a = (u8)(color.a * g_u8MaxPlusOneRoundDown),
  };
}

static GeoColor atx_sample_cube(const AssetTextureComp** textures, const GeoVector dir) {
  struct AtxCubePoint     point = atx_cube_lookup(dir);
  const AssetTextureComp* tex   = textures[point.face];
  return asset_texture_sample(tex, point.coordX, point.coordY, 0);
}

/**
 * Copy all pixel data to the output.
 * NOTE: Requires all input textures as well as the output textures to have matching sizes.
 */
static void atx_write_simple(const AtxDef* def, const AssetTextureComp** textures, Mem dest) {
  for (usize i = 0; i != def->textures.count; ++i) {
    const Mem texMem = asset_texture_data(textures[i]);
    mem_cpy(dest, texMem);
    dest = mem_consume(dest, texMem.size);
  }
  diag_assert(!dest.size); // Verify we filled the entire output.
}

/**
 * Sample all pixels on all textures from the input textures.
 * NOTE: Supports differently sized input and output textures.
 */
static void atx_write_resample(
    const AtxDef*            def,
    const AssetTextureComp** textures,
    const u32                width,
    const u32                height,
    const bool               srgb,
    Mem                      dest) {
  const f32 invWidth  = 1.0f / width;
  const f32 invHeight = 1.0f / height;
  for (usize texIndex = 0; texIndex != def->textures.count; ++texIndex) {
    const AssetTextureComp* tex = textures[texIndex];
    // TODO: Support input textures with multiple layers.
    diag_assert(tex->layers <= 1);

    for (u32 y = 0; y != height; ++y) {
      const f32 yFrac = (y + 0.5f) * invHeight;
      for (u32 x = 0; x != width; ++x) {
        const f32      xFrac = (x + 0.5f) * invWidth;
        const GeoColor color = asset_texture_sample(tex, xFrac, yFrac, 0);

        if (srgb) {
          *((AssetTexturePixelB4*)dest.ptr) = atx_color_to_b4_srgb(color);
        } else {
          *((AssetTexturePixelB4*)dest.ptr) = atx_color_to_b4_linear(color);
        }
        dest = mem_consume(dest, sizeof(AssetTexturePixelB4));
      }
    }
  }
  diag_assert(!dest.size); // Verify we filled the entire output.
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

/**
 * Compute the diffuse irradiance at the given direction.
 * Takes samples from a hemisphere pointing in the given direction and combines the radiance.
 */
static GeoColor
atx_diff_irradiance_convolve(const AssetTextureComp** textures, const GeoVector fwd) {
  const GeoQuat rot = geo_quat_look(fwd, geo_up);

  static const f32 g_sampleDelta = 0.075f;
  static const f32 g_piTwo       = math_pi_f32 * 2.0f;
  static const f32 g_piHalf      = math_pi_f32 * 0.5f;

  GeoColor irradiance = geo_color(0, 0, 0, 0);
  f32      numSamples = 0;
  for (f32 phi = 0.0; phi < g_piTwo; phi += g_sampleDelta) {
    const f32 cosPhi = math_cos_f32(phi);
    const f32 sinPhi = math_sin_f32(phi);

    for (f32 theta = 0.0; theta < g_piHalf; theta += g_sampleDelta) {
      const f32 cosTheta = math_cos_f32(theta);
      const f32 sinTheta = math_sin_f32(theta);

      // Convert the spherical coordinates to cartesian coordinates in tangent space.
      const GeoVector tangentDir = geo_vector(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
      const GeoColor  radiance   = atx_sample_cube(textures, geo_quat_rotate(rot, tangentDir));

      // Add the contribution of the sample.
      irradiance = geo_color_add(irradiance, geo_color_mul(radiance, cosTheta * sinTheta));
      ++numSamples;
    }
  }

  return geo_color_mul(irradiance, (1.0f / numSamples) * math_pi_f32);
}

/**
 * Generate a diffuse irradiance map.
 * NOTE: Supports differently sized input and output textures.
 */
static void atx_write_diff_irradiance_b4(
    const AtxDef* def, const AssetTextureComp** textures, const u32 size, Mem dest) {
  const f32 invSize = 1.0f / size;
  for (usize faceIdx = 0; faceIdx != def->textures.count; ++faceIdx) {
    for (u32 y = 0; y != size; ++y) {
      const f32 yFrac = (y + 0.5f) * invSize;
      for (u32 x = 0; x != size; ++x) {
        const f32 xFrac = (x + 0.5f) * invSize;

        const GeoVector posLocal   = geo_vector(xFrac * 2.0f - 1.0f, yFrac * 2.0f - 1.0f, 1.0f);
        const GeoVector dir        = geo_quat_rotate(g_cubeFaceRot[faceIdx], posLocal);
        const GeoColor  irradiance = atx_diff_irradiance_convolve(textures, dir);

        *((AssetTexturePixelB4*)dest.ptr) = atx_color_to_b4_linear(irradiance);
        dest                              = mem_consume(dest, sizeof(AssetTexturePixelB4));
      }
    }
  }
  diag_assert(!dest.size); // Verify we filled the entire output.
}

/**
 * Compute filtered specular irradiance for the specular lobe orientated in the given normal.
 * https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/
 */
static GeoColor atx_spec_irradiance_convolve(
    const AssetTextureComp** textures,
    const GeoVector          normal,
    const GeoVector*         samples,
    const u32                sampleCount) {
  const GeoQuat rot = geo_quat_look(normal, geo_up);

  GeoColor irradiance  = geo_color(0, 0, 0, 0);
  f32      totalWeight = 0.0f;
  for (u32 i = 0; i != sampleCount; ++i) {
    const GeoVector halfDirWorld = geo_quat_rotate(rot, samples[i]);

    const f32       nDotH    = geo_vector_dot(normal, halfDirWorld);
    const GeoVector lightDir = geo_vector_sub(geo_vector_mul(halfDirWorld, nDotH * 2.0f), normal);
    const f32       nDotL    = math_max(geo_vector_dot(normal, lightDir), 0.0);

    if (nDotL > 0.0f) {
      const GeoColor radiance = atx_sample_cube(textures, lightDir);
      irradiance              = geo_color_add(irradiance, geo_color_mul(radiance, nDotL));
      totalWeight += nDotL;
    }
  }

  return geo_color_div(irradiance, totalWeight);
}

/**
 * Generate a specular irradiance map (aka 'environment map').
 * Lowest mip represents roughness == 0 and the highest represents roughness == 1.
 * NOTE: Supports differently sized input and output textures.
 */
static void atx_write_spec_irradiance_b4(
    const AtxDef*              def,
    const AssetTextureComp**   textures,
    const AssetTextureType     type,
    const AssetTextureChannels channels,
    const u32                  size,
    Mem                        dest) {
  // Mip 0 represents a perfect mirror so we can just copy the source.
  const usize mip0Size = asset_texture_req_size(type, channels, size, size, 6, 1);
  atx_write_resample(def, textures, size, size, false, mem_slice(dest, 0, mip0Size));
  dest = mem_consume(dest, mip0Size);

  // Other mip-levels represent rougher specular irradiance so we convolve the incoming radiance.
  static const u32 g_sampleCounts[atx_spec_irradiance_mips] = {0, 64, 128, 256, 512};
  GeoVector        samples[512];
  for (u32 mipLevel = 1; mipLevel != atx_spec_irradiance_mips; ++mipLevel) {
    const u32 mipSize     = math_max(size >> mipLevel, 1);
    const f32 invMipSize  = 1.0f / mipSize;
    const f32 roughness   = mipLevel / (f32)(atx_spec_irradiance_mips - 1);
    const u32 sampleCount = g_sampleCounts[mipLevel];

    // Compute the sample points for this roughness.
    for (u32 i = 0; i != sampleCount; ++i) {
      samples[i] = importance_sample_ggx(i, sampleCount, roughness);
    }

    // Convolve all samples for all pixels.
    for (u32 faceIdx = 0; faceIdx != def->textures.count; ++faceIdx) {
      for (u32 y = 0; y != mipSize; ++y) {
        const f32 yFrac = (y + 0.5f) * invMipSize;
        for (u32 x = 0; x != mipSize; ++x) {
          const f32 xFrac = (x + 0.5f) * invMipSize;

          const GeoVector posLocal = geo_vector(xFrac * 2.0f - 1.0f, yFrac * 2.0f - 1.0f, 1.0f);
          const GeoVector dir      = geo_quat_rotate(g_cubeFaceRot[faceIdx], posLocal);
          const GeoColor  irr = atx_spec_irradiance_convolve(textures, dir, samples, sampleCount);

          *((AssetTexturePixelB4*)dest.ptr) = atx_color_to_b4_linear(irr);
          dest                              = mem_consume(dest, sizeof(AssetTexturePixelB4));
        }
      }
    }
  }
  diag_assert(!dest.size); // Verify we filled the entire output.
}

static void atx_generate(
    const AtxDef*            def,
    const AssetTextureComp** textures,
    AssetTextureComp*        outTexture,
    AtxError*                err) {

  const AssetTextureType     type     = textures[0]->type;
  const AssetTextureChannels channels = textures[0]->channels;
  const bool                 inSrgb   = (textures[0]->flags & AssetTextureFlags_Srgb) != 0;
  const u32                  inWidth  = textures[0]->width;
  const u32                  inHeight = textures[0]->height;
  u32                        layers   = math_max(1, textures[0]->layers);

  const bool irradianceMap = atx_output_irradiance(def);
  if (UNLIKELY(irradianceMap && type != AssetTextureType_U8)) {
    // TODO: Support hdr input texture for cube-irradiance maps.
    *err = AtxError_InvalidCubeIrradianceInputType;
    return;
  }

  for (usize i = 1; i != def->textures.count; ++i) {
    if (UNLIKELY(textures[i]->type != type)) {
      *err = AtxError_MismatchType;
      return;
    }
    if (UNLIKELY(textures[i]->channels != channels)) {
      *err = AtxError_MismatchChannels;
      return;
    }
    if (UNLIKELY(inSrgb != ((textures[i]->flags & AssetTextureFlags_Srgb) != 0))) {
      *err = AtxError_MismatchEncoding;
      return;
    }
    if (UNLIKELY(textures[i]->width != inWidth || textures[i]->height != inHeight)) {
      *err = AtxError_MismatchSize;
      return;
    }
    layers += math_max(1, textures[i]->layers);
  }
  if (UNLIKELY(layers > atx_max_layers)) {
    *err = AtxError_TooManyLayers;
    return;
  }

  const u32  outWidth      = def->sizeX ? def->sizeX : inWidth;
  const u32  outHeight     = def->sizeY ? def->sizeY : inHeight;
  const bool needsResample = inWidth != outWidth || inHeight != outHeight;
  if (UNLIKELY(needsResample && (type != AssetTextureType_U8 || channels != 4))) {
    // TODO: Support resampling hdr input textures.
    *err = AtxError_UnsupportedInputTypeForResampling;
    return;
  }
  const bool isCubeMap = atx_output_cube(def);
  if (UNLIKELY(isCubeMap && outWidth != outHeight)) {
    *err = AtxError_InvalidCubeAspect;
    return;
  }
  if (UNLIKELY(isCubeMap && layers != 6)) {
    *err = AtxError_InvalidCubeTextureCount;
    return;
  }

  const u32   mips      = atx_output_mips(def);
  const usize dataSize  = asset_texture_req_size(type, channels, outWidth, outHeight, layers, mips);
  const usize dataAlign = asset_texture_req_align(type, channels);
  const Mem   pixelsMem = alloc_alloc(g_alloc_heap, dataSize, dataAlign);

  bool outSrgb = irradianceMap ? false : inSrgb;
  switch (def->type) {
  case AtxType_Array:
  case AtxType_Cube:
    if (needsResample) {
      atx_write_resample(def, textures, outWidth, outHeight, outSrgb, pixelsMem);
    } else {
      atx_write_simple(def, textures, pixelsMem);
    }
    break;
  case AtxType_CubeDiffIrradiance:
    atx_write_diff_irradiance_b4(def, textures, outWidth, pixelsMem);
    break;
  case AtxType_CubeSpecIrradiance:
    atx_write_spec_irradiance_b4(def, textures, type, channels, outWidth, pixelsMem);
    break;
  }

  *outTexture = (AssetTextureComp){
      .type         = type,
      .channels     = channels,
      .flags        = atx_texture_flags(def, outSrgb),
      .pixelsRaw    = pixelsMem.ptr,
      .width        = outWidth,
      .height       = outHeight,
      .layers       = layers,
      .srcMipLevels = mips,
  };
  *err = AtxError_None;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetAtxLoadComp); }
ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

/**
 * Acquire all textures.
 */
ecs_system_define(AtxLoadAcquireSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetAtxLoadComp* load   = ecs_view_write_t(itr, AssetAtxLoadComp);

    if (load->textures.size) {
      continue; // Already acquired textures.
    }

    /**
     * Acquire all textures.
     */
    array_ptr_for_t(load->def.textures, String, texName) {
      const EcsEntityId texAsset                     = asset_lookup(world, manager, *texName);
      *dynarray_push_t(&load->textures, EcsEntityId) = texAsset;
      asset_acquire(world, texAsset);
      asset_register_dep(world, entity, texAsset);
    }
  }
}

/**
 * Update all active loads.
 */
ecs_system_define(AtxLoadUpdateSys) {
  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  u32 numGenerates = 0;

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetAtxLoadComp* load   = ecs_view_write_t(itr, AssetAtxLoadComp);
    AtxError          err;

    if (!load->textures.size) {
      goto Next; // Textures not yet acquired.
    }

    /**
     * Gather all textures.
     */
    const AssetTextureComp** textures = mem_stack(sizeof(void*) * load->textures.size).ptr;
    for (usize i = 0; i != load->textures.size; ++i) {
      const EcsEntityId texAsset = *dynarray_at_t(&load->textures, i, EcsEntityId);
      if (ecs_world_has_t(world, texAsset, AssetFailedComp)) {
        err = AtxError_InvalidTexture;
        goto Error;
      }
      if (!ecs_world_has_t(world, texAsset, AssetLoadedComp)) {
        goto Next; // Wait for the texture to be loaded.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(textureItr, texAsset))) {
        err = AtxError_InvalidTexture;
        goto Error;
      }
      textures[i] = ecs_view_read_t(textureItr, AssetTextureComp);
    }

    AssetTextureComp texture;
    atx_generate(&load->def, textures, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load Atx array-texture", log_param("error", fmt_text(atx_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetAtxLoadComp);
    dynarray_for_t(&load->textures, EcsEntityId, texAsset) { asset_release(world, *texAsset); }

  Next:
    if (++numGenerates == atx_max_generates_per_tick) {
      break;
    }
    continue;
  }
}

ecs_module_init(asset_atx_module) {
  atx_datareg_init();

  ecs_register_comp(AssetAtxLoadComp, .destructor = ecs_destruct_atx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);

  ecs_register_system(AtxLoadAcquireSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(AtxLoadUpdateSys, ecs_view_id(LoadView), ecs_view_id(TextureView));
}

void asset_load_atx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  String         errMsg;
  AtxDef         def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataAtxDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!def.textures.count)) {
    errMsg = atx_error_str(AtxError_NoTextures);
    goto Error;
  }
  if (UNLIKELY(def.textures.count > atx_max_textures)) {
    errMsg = atx_error_str(AtxError_TooManyTextures);
    goto Error;
  }
  if (UNLIKELY(def.sizeX > atx_max_size || def.sizeY > atx_max_size)) {
    errMsg = atx_error_str(AtxError_SizeTooBig);
    goto Error;
  }
  array_ptr_for_t(def.textures, String, texName) {
    if (UNLIKELY(string_is_empty(*texName))) {
      errMsg = atx_error_str(AtxError_InvalidTexture);
      goto Error;
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetAtxLoadComp,
      .def      = def,
      .textures = dynarray_create_t(g_alloc_heap, EcsEntityId, def.textures.count));
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load atx texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtxDefMeta, mem_var(def));
  asset_repo_source_close(src);
}
