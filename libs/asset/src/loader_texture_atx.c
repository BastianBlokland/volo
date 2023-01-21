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

static DataReg* g_dataReg;
static DataMeta g_dataAtxDefMeta;

typedef enum {
  AtxType_Array,
  AtxType_Cube,
  AtxType_CubeIrradiance,
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
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_enum_t(g_dataReg, AtxType);
    data_reg_const_t(g_dataReg, AtxType, Array);
    data_reg_const_t(g_dataReg, AtxType, Cube);
    data_reg_const_t(g_dataReg, AtxType, CubeIrradiance);

    data_reg_struct_t(g_dataReg, AtxDef);
    data_reg_field_t(g_dataReg, AtxDef, type, t_AtxType);
    data_reg_field_t(g_dataReg, AtxDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtxDef, sizeX, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtxDef, sizeY, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtxDef, textures, data_prim_t(String), .flags = DataFlags_NotEmpty, .container = DataContainer_Array);
    // clang-format on

    g_dataAtxDefMeta = data_meta_t(t_AtxDef);
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
  case AtxType_CubeIrradiance:
    flags |= AssetTextureFlags_CubeMap;
    break;
  }
  if (def->mipmaps) {
    flags |= AssetTextureFlags_MipMaps;
  }
  if (srgb) {
    flags |= AssetTextureFlags_Srgb;
  }
  return flags;
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
      .a = (u8)(math_pow_f32(color.a, g_gammaInv) * g_u8MaxPlusOneRoundDown),
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
    diag_assert(tex->layers == 0);

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
 * Compute the irradiance at the given direction.
 * Takes samples from hemisphere pointing in the given direction and combines the radiance.
 */
static GeoColor atx_irradiance_convolve(const AssetTextureComp** textures, const GeoVector fwd) {
  const GeoVector right = geo_vector_norm(geo_vector_cross3(geo_up, fwd));
  const GeoVector up    = geo_vector_norm(geo_vector_cross3(fwd, right));

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

      // Convert tangent dir to world space.
      GeoVector worldDir = geo_vector(0);
      worldDir           = geo_vector_add(worldDir, geo_vector_mul(right, tangentDir.x));
      worldDir           = geo_vector_add(worldDir, geo_vector_mul(up, tangentDir.y));
      worldDir           = geo_vector_add(worldDir, geo_vector_mul(fwd, tangentDir.z));

      // Sample the emitted radiance from this direction.
      const GeoColor radiance = atx_sample_cube(textures, worldDir);

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
static void atx_write_irradiance_b4(
    const AtxDef*            def,
    const AssetTextureComp** textures,
    const u32                width,
    const u32                height,
    Mem                      dest) {
  const f32     invWidth  = 1.0f / width;
  const f32     invHeight = 1.0f / height;
  const GeoQuat faceRot[] = {
      geo_quat_forward_to_right,
      geo_quat_forward_to_left,
      geo_quat_forward_to_down,
      geo_quat_forward_to_up,
      geo_quat_forward_to_forward,
      geo_quat_forward_to_backward,
  };
  for (usize faceIdx = 0; faceIdx != def->textures.count; ++faceIdx) {
    for (u32 y = 0; y != height; ++y) {
      const f32 yFrac = (y + 0.5f) * invHeight;
      for (u32 x = 0; x != width; ++x) {
        const f32 xFrac = (x + 0.5f) * invWidth;

        const GeoVector posLocal   = geo_vector(xFrac * 2.0f - 1.0f, yFrac * 2.0f - 1.0f, 1.0f);
        const GeoVector dir        = geo_quat_rotate(faceRot[faceIdx], posLocal);
        const GeoColor  irradiance = atx_irradiance_convolve(textures, dir);

        *((AssetTexturePixelB4*)dest.ptr) = atx_color_to_b4_linear(irradiance);
        dest                              = mem_consume(dest, sizeof(AssetTexturePixelB4));
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

  if (UNLIKELY(def->type == AtxType_CubeIrradiance && type != AssetTextureType_U8)) {
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
  const bool isCubeMap = def->type == AtxType_Cube || def->type == AtxType_CubeIrradiance;
  if (UNLIKELY(isCubeMap && outWidth != outHeight)) {
    *err = AtxError_InvalidCubeAspect;
    return;
  }
  if (UNLIKELY(isCubeMap && layers != 6)) {
    *err = AtxError_InvalidCubeTextureCount;
    return;
  }

  const usize pixelDataSize   = asset_texture_pixel_size(textures[0]);
  const usize textureDataSize = outWidth * outHeight * pixelDataSize * layers;
  const Mem   pixelsMem       = alloc_alloc(g_alloc_heap, textureDataSize, pixelDataSize);

  bool outSrgb = inSrgb;
  switch (def->type) {
  case AtxType_Array:
  case AtxType_Cube:
    if (needsResample) {
      atx_write_resample(def, textures, outWidth, outHeight, outSrgb, pixelsMem);
    } else {
      atx_write_simple(def, textures, pixelsMem);
    }
    break;
  case AtxType_CubeIrradiance:
    atx_write_irradiance_b4(def, textures, outWidth, outHeight, pixelsMem);
    outSrgb = false; // Always output irradiance maps in linear encoding.
    break;
  }

  *outTexture = (AssetTextureComp){
      .type      = type,
      .channels  = channels,
      .flags     = atx_texture_flags(def, outSrgb),
      .pixelsRaw = pixelsMem.ptr,
      .width     = outWidth,
      .height    = outHeight,
      .layers    = layers,
  };
  *err = AtxError_None;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetAtxLoadComp); }
ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

/**
 * Update all active loads.
 */
ecs_system_define(AtxLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetAtxLoadComp* load   = ecs_view_write_t(itr, AssetAtxLoadComp);
    AtxError          err;

    /**
     * Start loading all textures.
     */
    if (!load->textures.size) {
      array_ptr_for_t(load->def.textures, String, texName) {
        const EcsEntityId texAsset                     = asset_lookup(world, manager, *texName);
        *dynarray_push_t(&load->textures, EcsEntityId) = texAsset;
        asset_acquire(world, texAsset);
        asset_register_dep(world, entity, texAsset);
      }
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
    continue;
  }
}

ecs_module_init(asset_atx_module) {
  atx_datareg_init();

  ecs_register_comp(AssetAtxLoadComp, .destructor = ecs_destruct_atx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);

  ecs_register_system(
      AtxLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(TextureView));
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
