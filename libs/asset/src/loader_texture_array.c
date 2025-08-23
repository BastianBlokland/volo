#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/math.h"
#include "data/read.h"
#include "data/registry.h"
#include "data/utils.h"
#include "ecs/entity.h"
#include "ecs/utils.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "geo/color.h"
#include "geo/quat.h"

#include "loader_texture.h"
#include "manager.h"
#include "repo.h"

#define arraytex_max_textures 100
#define arraytex_max_size 2048
#define arraytex_max_generates_per_tick 1
#define arraytex_spec_irradiance_mips 5

static const GeoQuat g_cubeFaceRot[] = {
    {0, 0.7071068f, 0, 0.7071068f},  // Forward to right.
    {0, -0.7071068f, 0, 0.7071068f}, // Forward to left.
    {0.7071068f, 0, 0, 0.7071068f},  // Forward to down.
    {-0.7071068f, 0, 0, 0.7071068f}, // Forward to up.
    {0, 0, 0, 1},                    // Forward to forward.
    {0, 1, 0, 0},                    // Forward to backward.
};

DataMeta g_assetTexArrayDefMeta;

typedef enum {
  ArrayTexType_Array,
  ArrayTexType_Cube,
  ArrayTexType_CubeDiffIrradiance,
  ArrayTexType_CubeSpecIrradiance,
} ArrayTexType;

typedef enum {
  ArrayTexChannels_One   = 1,
  ArrayTexChannels_Two   = 2,
  ArrayTexChannels_Three = 3,
  ArrayTexChannels_Four  = 4,
} ArrayTexChannels;

typedef struct {
  ArrayTexType     type;
  ArrayTexChannels channels;
  bool             mipmaps, srgb, lossless, nearest;
  u32              sizeX, sizeY;
  HeapArray_t(String) textures;
} ArrayTexDef;

ecs_comp_define(AssetArrayLoadComp) {
  ArrayTexDef def;
  DynArray    textures; // EcsEntityId[].
};

static void ecs_destruct_arraytex_load_comp(void* data) {
  AssetArrayLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetTexArrayDefMeta, mem_var(comp->def));
  dynarray_destroy(&comp->textures);
}

typedef enum {
  ArrayTexError_None = 0,
  ArrayTexError_NoTextures,
  ArrayTexError_TooManyTextures,
  ArrayTexError_SizeTooBig,
  ArrayTexError_TooFewChannelsForSrgb,
  ArrayTexError_InvalidTexture,
  ArrayTexError_InvalidTextureLayerCount,
  ArrayTexError_InvalidCubeAspect,
  ArrayTexError_InvalidCubeTextureCount,
  ArrayTexError_InvalidCubeIrradianceInputType,
  ArrayTexError_InvalidCubeIrradianceOutputSize,

  ArrayTexError_Count,
} ArrayTexError;

static String arraytex_error_str(const ArrayTexError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("ArrayTex does not specify any textures"),
      string_static("ArrayTex specifies more textures then are supported"),
      string_static("ArrayTex specifies a size larger then is supported"),
      string_static("ArrayTex specifies Srgb with less then 3 channels"),
      string_static("ArrayTex specifies an invalid texture"),
      string_static("ArrayTex specifies a texture with too many layers"),
      string_static("ArrayTex cube / cube-irradiance needs to be square"),
      string_static("ArrayTex cube / cube-irradiance needs 6 textures"),
      string_static("ArrayTex cube-irradiance needs rgba 8bit input textures"),
      string_static("ArrayTex specifies a size smaller then is supported for spec irradiance"),
  };
  ASSERT(array_elems(g_msgs) == ArrayTexError_Count, "Incorrect number of array-error messages");
  return g_msgs[err];
}

static bool arraytex_output_cube(const ArrayTexDef* def) {
  switch (def->type) {
  case ArrayTexType_Array:
    return false;
  case ArrayTexType_Cube:
  case ArrayTexType_CubeDiffIrradiance:
  case ArrayTexType_CubeSpecIrradiance:
    return true;
  }
  diag_crash();
}

static u32 arraytex_output_mips(const ArrayTexDef* def) {
  switch (def->type) {
  case ArrayTexType_Array:
  case ArrayTexType_Cube:
  case ArrayTexType_CubeDiffIrradiance:
    return 1;
  case ArrayTexType_CubeSpecIrradiance:
    return arraytex_spec_irradiance_mips;
  }
  diag_crash();
}

static AssetTextureFlags arraytex_output_flags(const ArrayTexDef* def) {
  AssetTextureFlags flags = 0;
  if (arraytex_output_cube(def)) {
    flags |= AssetTextureFlags_CubeMap;
  } else {
    flags |= AssetTextureFlags_Array;
  }
  if (def->mipmaps) {
    flags |= AssetTextureFlags_GenerateMips;
  }
  if (def->lossless) {
    flags |= AssetTextureFlags_Lossless;
  }
  if (def->srgb) {
    flags |= AssetTextureFlags_Srgb;
  }
  return flags;
}

typedef struct {
  u32 face;
  f32 coordX, coordY;
} CubePoint;

static CubePoint arraytex_cube_lookup(const GeoVector dir) {
  CubePoint       res;
  f32             scale;
  const GeoVector dirAbs = geo_vector_abs(dir);
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
  res.coordX = math_max(0, res.coordX * scale + 0.5f);
  res.coordY = math_max(0, res.coordY * scale + 0.5f);
  return res;
}

static void arraytex_color_write(const ArrayTexDef* def, const GeoColor color, u8* out) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;

  switch (def->channels) {
  case ArrayTexChannels_Four:
    out[3] = (u8)(color.a * g_u8MaxPlusOneRoundDown);
    // Fallthrough.
  case ArrayTexChannels_Three:
    out[2] = (u8)(color.b * g_u8MaxPlusOneRoundDown);
    // Fallthrough.
  case ArrayTexChannels_Two:
    out[1] = (u8)(color.g * g_u8MaxPlusOneRoundDown);
    // Fallthrough.
  case ArrayTexChannels_One:
    out[0] = (u8)(color.r * g_u8MaxPlusOneRoundDown);
    break;
  }
}

static GeoColor arraytex_sample_cube(const AssetTextureComp** textures, const GeoVector dir) {
  const CubePoint         point = arraytex_cube_lookup(dir);
  const AssetTextureComp* tex   = textures[point.face];
  return asset_texture_sample(tex, point.coordX, point.coordY, 0);
}

/**
 * Sample all pixels from all textures from the input textures.
 */
static void arraytex_write_simple(
    const ArrayTexDef*       def,
    const AssetTextureComp** textures,
    const u32                width,
    const u32                height,
    const bool               srgb,
    Mem                      dest) {
  const f32 invWidth  = 1.0f / width;
  const f32 invHeight = 1.0f / height;
  for (usize texIndex = 0; texIndex != def->textures.count; ++texIndex) {
    const AssetTextureComp* tex = textures[texIndex];

    for (u32 y = 0; y != height; ++y) {
      const f32 yFrac = (y + 0.5f) * invHeight;
      for (u32 x = 0; x != width; ++x) {
        const f32 xFrac = (x + 0.5f) * invWidth;
        GeoColor  color;
        if (def->nearest) {
          color = asset_texture_sample_nearest(tex, xFrac, yFrac, 0 /* layer */);
        } else {
          color = asset_texture_sample(tex, xFrac, yFrac, 0 /* layer */);
        }

        if (srgb) {
          color = geo_color_linear_to_srgb(color);
        }
        arraytex_color_write(def, color, dest.ptr);
        dest = mem_consume(dest, def->channels);
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
arraytex_diff_irradiance_convolve(const AssetTextureComp** textures, const GeoVector fwd) {
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
      const GeoColor  radiance   = arraytex_sample_cube(textures, geo_quat_rotate(rot, tangentDir));

      // Add the contribution of the sample.
      irradiance = geo_color_add(irradiance, geo_color_mul(radiance, cosTheta * sinTheta));
      ++numSamples;
    }
  }

  return geo_color_mul(irradiance, (1.0f / numSamples) * math_pi_f32);
}

/**
 * Generate a diffuse irradiance map.
 */
static void arraytex_write_diff_irradiance_b4(
    const ArrayTexDef* def, const AssetTextureComp** textures, const u32 size, Mem dest) {
  const f32 invSize = 1.0f / size;
  for (usize faceIdx = 0; faceIdx != def->textures.count; ++faceIdx) {
    for (u32 y = 0; y != size; ++y) {
      const f32 yFrac = (y + 0.5f) * invSize;
      for (u32 x = 0; x != size; ++x) {
        const f32 xFrac = (x + 0.5f) * invSize;

        const GeoVector posLocal   = geo_vector(xFrac * 2.0f - 1.0f, yFrac * 2.0f - 1.0f, 1.0f);
        const GeoVector dir        = geo_quat_rotate(g_cubeFaceRot[faceIdx], posLocal);
        const GeoColor  irradiance = arraytex_diff_irradiance_convolve(textures, dir);

        arraytex_color_write(def, irradiance, dest.ptr);
        dest = mem_consume(dest, def->channels);
      }
    }
  }
  diag_assert(!dest.size); // Verify we filled the entire output.
}

/**
 * Compute filtered specular irradiance for the specular lobe orientated in the given normal.
 * https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/
 */
static GeoColor arraytex_spec_irradiance_convolve(
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
      const GeoColor radiance = arraytex_sample_cube(textures, lightDir);
      irradiance              = geo_color_add(irradiance, geo_color_mul(radiance, nDotL));
      totalWeight += nDotL;
    }
  }

  return geo_color_div(irradiance, totalWeight);
}

/**
 * Generate a specular irradiance map (aka 'environment map').
 * Lowest mip represents roughness == 0 and the highest represents roughness == 1.
 */
static void arraytex_write_spec_irradiance_b4(
    const ArrayTexDef* def, const AssetTextureComp** textures, const u32 size, Mem dest) {
  // Mip 0 represents a perfect mirror so we can just copy the source.
  const usize mip0Size =
      asset_texture_type_mip_size(AssetTextureType_u8, def->channels, size, size, 6, 0);
  arraytex_write_simple(def, textures, size, size, false, mem_slice(dest, 0, mip0Size));
  dest = mem_consume(dest, mip0Size);

  // Other mip-levels represent rougher specular irradiance so we convolve the incoming radiance.
  static const u32 g_sampleCounts[arraytex_spec_irradiance_mips] = {0, 64, 128, 256, 512};
  GeoVector        samples[512];
  for (u32 mipLevel = 1; mipLevel != arraytex_spec_irradiance_mips; ++mipLevel) {
    const u32 mipSize     = math_max(size >> mipLevel, 1);
    const f32 invMipSize  = 1.0f / mipSize;
    const f32 roughness   = mipLevel / (f32)(arraytex_spec_irradiance_mips - 1);
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
          const GeoColor  irr =
              arraytex_spec_irradiance_convolve(textures, dir, samples, sampleCount);

          arraytex_color_write(def, irr, dest.ptr);
          dest = mem_consume(dest, def->channels);
        }
      }
    }
  }
  diag_assert(!dest.size); // Verify we filled the entire output.
}

static void arraytex_generate(
    const ArrayTexDef*       def,
    const AssetTextureComp** textures,
    AssetTextureComp*        outTexture,
    ArrayTexError*           err) {

  // Validate textures.
  for (u32 i = 0; i != def->textures.count; ++i) {
    if (UNLIKELY(textures[i]->layers > 1)) {
      *err = ArrayTexError_InvalidTextureLayerCount;
      return;
    }
  }

  const u32 layers = (u32)def->textures.count;
  const u32 width  = def->sizeX ? def->sizeX : textures[0]->width;
  const u32 height = def->sizeY ? def->sizeY : textures[0]->height;

  // Validate settings.
  const bool isCubeMap = arraytex_output_cube(def);
  if (UNLIKELY(isCubeMap && width != height)) {
    *err = ArrayTexError_InvalidCubeAspect;
    return;
  }
  if (UNLIKELY(isCubeMap && layers != 6)) {
    *err = ArrayTexError_InvalidCubeTextureCount;
    return;
  }
  if (UNLIKELY(def->type == ArrayTexType_CubeSpecIrradiance && width < 64)) {
    *err = ArrayTexError_InvalidCubeIrradianceOutputSize;
    return;
  }

  // Allocate pixel memory.
  const u32   mips = arraytex_output_mips(def);
  const usize dataSize =
      asset_texture_type_size(AssetTextureType_u8, def->channels, width, height, layers, mips);
  const Mem pixelsMem = alloc_alloc(g_allocHeap, dataSize, sizeof(u8));

  // Fill pixels.
  switch (def->type) {
  case ArrayTexType_Array:
  case ArrayTexType_Cube:
    arraytex_write_simple(def, textures, width, height, def->srgb, pixelsMem);
    break;
  case ArrayTexType_CubeDiffIrradiance:
    arraytex_write_diff_irradiance_b4(def, textures, width, pixelsMem);
    break;
  case ArrayTexType_CubeSpecIrradiance:
    arraytex_write_spec_irradiance_b4(def, textures, width, pixelsMem);
    break;
  }

  // Create texture.
  *outTexture = asset_texture_create(
      pixelsMem,
      width,
      height,
      def->channels,
      layers,
      mips,
      0 /* mipsMax */,
      AssetTextureType_u8,
      arraytex_output_flags(def));

  // Cleanup.
  *err = ArrayTexError_None;
  alloc_free(g_allocHeap, pixelsMem);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_write(AssetComp);
  ecs_access_write(AssetArrayLoadComp);
}

ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

/**
 * Acquire all textures.
 */
ecs_system_define(ArrayTexLoadAcquireSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId   entity = ecs_view_entity(itr);
    AssetArrayLoadComp* load   = ecs_view_write_t(itr, AssetArrayLoadComp);

    if (load->textures.size) {
      continue; // Already acquired textures.
    }

    /**
     * Acquire all textures.
     */
    heap_array_for_t(load->def.textures, String, texName) {
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
ecs_system_define(ArrayTexLoadUpdateSys) {
  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  u32 numGenerates = 0;

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId   entity = ecs_view_entity(itr);
    const String        id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetArrayLoadComp* load   = ecs_view_write_t(itr, AssetArrayLoadComp);
    ArrayTexError       err;

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
        err = ArrayTexError_InvalidTexture;
        goto Error;
      }
      if (!ecs_world_has_t(world, texAsset, AssetLoadedComp)) {
        goto Next; // Wait for the texture to be loaded.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(textureItr, texAsset))) {
        err = ArrayTexError_InvalidTexture;
        goto Error;
      }
      textures[i] = ecs_view_read_t(textureItr, AssetTextureComp);
    }

    AssetTextureComp texture;
    arraytex_generate(&load->def, textures, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
    asset_mark_load_success(world, entity);
    asset_cache(world, entity, g_assetTexMeta, mem_var(texture));
    goto Cleanup;

  Error:
    asset_mark_load_failure(world, entity, id, arraytex_error_str(err), (i32)err);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetArrayLoadComp);
    dynarray_for_t(&load->textures, EcsEntityId, texAsset) { asset_release(world, *texAsset); }

  Next:
    if (++numGenerates == arraytex_max_generates_per_tick) {
      break;
    }
    continue;
  }
}

ecs_module_init(asset_texture_array_module) {
  ecs_register_comp(AssetArrayLoadComp, .destructor = ecs_destruct_arraytex_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);

  ecs_register_system(ArrayTexLoadAcquireSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(ArrayTexLoadUpdateSys, ecs_view_id(LoadView), ecs_view_id(TextureView));
}

void asset_data_init_arraytex(void) {
  // clang-format off
  data_reg_enum_t(g_dataReg, ArrayTexType);
  data_reg_const_t(g_dataReg, ArrayTexType, Array);
  data_reg_const_t(g_dataReg, ArrayTexType, Cube);
  data_reg_const_t(g_dataReg, ArrayTexType, CubeDiffIrradiance);
  data_reg_const_t(g_dataReg, ArrayTexType, CubeSpecIrradiance);

  data_reg_enum_t(g_dataReg, ArrayTexChannels);
  data_reg_const_t(g_dataReg, ArrayTexChannels, One);
  data_reg_const_t(g_dataReg, ArrayTexChannels, Two);
  data_reg_const_t(g_dataReg, ArrayTexChannels, Three);
  data_reg_const_t(g_dataReg, ArrayTexChannels, Four);

  data_reg_struct_t(g_dataReg, ArrayTexDef);
  data_reg_field_t(g_dataReg, ArrayTexDef, type, t_ArrayTexType);
  data_reg_field_t(g_dataReg, ArrayTexDef, channels, t_ArrayTexChannels);
  data_reg_field_t(g_dataReg, ArrayTexDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, ArrayTexDef, srgb, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, ArrayTexDef, lossless, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, ArrayTexDef, nearest, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, ArrayTexDef, sizeX, data_prim_t(u32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, ArrayTexDef, sizeY, data_prim_t(u32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, ArrayTexDef, textures, data_prim_t(String), .flags = DataFlags_NotEmpty, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetTexArrayDefMeta = data_meta_t(t_ArrayTexDef);
}

void asset_load_tex_array(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  String         errMsg;
  ArrayTexDef    def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_allocHeap, g_assetTexArrayDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!def.textures.count)) {
    errMsg = arraytex_error_str(ArrayTexError_NoTextures);
    goto Error;
  }
  if (UNLIKELY(def.textures.count > arraytex_max_textures)) {
    errMsg = arraytex_error_str(ArrayTexError_TooManyTextures);
    goto Error;
  }
  if (UNLIKELY(def.sizeX > arraytex_max_size || def.sizeY > arraytex_max_size)) {
    errMsg = arraytex_error_str(ArrayTexError_SizeTooBig);
    goto Error;
  }
  if (UNLIKELY(def.srgb && def.channels < ArrayTexChannels_Three)) {
    errMsg = arraytex_error_str(ArrayTexError_TooFewChannelsForSrgb);
    goto Error;
  }

  heap_array_for_t(def.textures, String, texName) {
    if (UNLIKELY(string_is_empty(*texName))) {
      errMsg = arraytex_error_str(ArrayTexError_InvalidTexture);
      goto Error;
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetArrayLoadComp,
      .def      = def,
      .textures = dynarray_create_t(g_allocHeap, EcsEntityId, def.textures.count));
  asset_repo_close(src);
  return;

Error:
  data_destroy(g_dataReg, g_allocHeap, g_assetTexArrayDefMeta, mem_var(def));
  asset_repo_close(src);
  asset_mark_load_failure(world, entity, id, errMsg, -1 /* errorCode */);
}
