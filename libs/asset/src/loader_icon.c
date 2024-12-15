#include "asset_icon.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define icon_max_width 64
#define icon_max_height 64

DataMeta g_assetIconDefMeta;
DataMeta g_assetIconMeta;

typedef struct {
  String    texture;
  u32       hotspotX, hotspotY;
  f32       scale;
  GeoColor* color;
} IconDef;

typedef enum {
  IconError_None,
  IconError_InvalidTexture,
  IconError_TextureTooBig,

  IconError_Count,
} IconError;

static String icon_error_str(const IconError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Icon specifies an invalid texture"),
      string_static("Icon texture size exceeds the maximum"),
  };
  ASSERT(array_elems(g_msgs) == IconError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

ecs_comp_define_public(AssetIconComp);

ecs_comp_define(AssetIconLoadComp) {
  IconDef     def;
  EcsEntityId textureAsset;
};

ecs_comp_define(AssetIconSourceComp) { AssetSource* src; };

static void ecs_destruct_icon_comp(void* data) {
  AssetIconComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetIconMeta, mem_create(comp, sizeof(AssetIconComp)));
}

static void ecs_destruct_icon_load_comp(void* data) {
  AssetIconLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetIconDefMeta, mem_var(comp->def));
}

static void ecs_destruct_icon_source_comp(void* data) {
  AssetIconSourceComp* comp = data;
  asset_repo_source_close(comp->src);
}

static AssetIconPixel asset_icon_pixel(const GeoColor color) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;
  return (AssetIconPixel){
      .r = (u8)(color.r * g_u8MaxPlusOneRoundDown),
      .g = (u8)(color.g * g_u8MaxPlusOneRoundDown),
      .b = (u8)(color.b * g_u8MaxPlusOneRoundDown),
      .a = (u8)(color.a * g_u8MaxPlusOneRoundDown),
  };
}

static void
asset_icon_generate(const IconDef* def, const AssetTextureComp* texture, AssetIconComp* outIcon) {
  const f32 scale     = def->scale < f32_epsilon ? 1.0f : def->scale;
  const u32 outWidth  = math_max((u32)math_round_nearest_f32(texture->width * scale), 1);
  const u32 outHeight = math_max((u32)math_round_nearest_f32(texture->height * scale), 1);

  const u32   pixelCount   = outWidth * outHeight;
  const usize pixelMemSize = sizeof(AssetIconPixel) * pixelCount;
  const Mem   pixelMem     = alloc_alloc(g_allocHeap, pixelMemSize, sizeof(AssetIconPixel));

  const bool      colored      = def->color != null;
  const GeoColor  colorMul     = def->color ? *def->color : geo_color_white;
  const f32       outWidthInv  = 1.0f / (f32)outWidth;
  const f32       outHeightInv = 1.0f / (f32)outHeight;
  AssetIconPixel* outPixels    = pixelMem.ptr;
  for (u32 y = 0; y != outHeight; ++y) {
    const f32 yNorm = (f32)(y + 0.5f) * outHeightInv;
    for (u32 x = 0; x != outWidth; ++x) {
      const f32 xNorm = (f32)(x + 0.5f) * outWidthInv;

      const u32 layer       = 0;
      GeoColor  colorLinear = asset_texture_sample(texture, xNorm, yNorm, layer);

      if (colored) {
        colorLinear = geo_color_mul_comps(colorLinear, colorMul);
        colorLinear = geo_color_clamp01(colorLinear);
      }

      // Always output Srgb encoded pixels.
      outPixels[y * outWidth + x] = asset_icon_pixel(geo_color_linear_to_srgb(colorLinear));
    }
  }

  outIcon->width     = outWidth;
  outIcon->height    = outHeight;
  outIcon->hotspotX  = math_min((u32)math_round_nearest_f32(def->hotspotX * scale), outWidth - 1);
  outIcon->hotspotY  = math_min((u32)math_round_nearest_f32(def->hotspotY * scale), outHeight - 1);
  outIcon->pixelData = data_mem_create(pixelMem);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_write(AssetIconLoadComp);
}

ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

ecs_view_define(UnloadView) {
  ecs_access_with(AssetIconComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load icon assets.
 */
ecs_system_define(LoadIconAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }

  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    const String       id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetIconLoadComp* load   = ecs_view_write_t(itr, AssetIconLoadComp);
    IconError          err;

    /**
     * Start loading the icon texture.
     */
    if (!load->textureAsset) {
      load->textureAsset = asset_lookup(world, manager, load->def.texture);
      asset_acquire(world, load->textureAsset);
      asset_register_dep(world, entity, load->textureAsset);
      goto Next; // Wait for the acquire to take effect.
    }

    /**
     * Wait for the icon texture.
     */
    if (ecs_world_has_t(world, load->textureAsset, AssetFailedComp)) {
      err = IconError_InvalidTexture;
      goto Error;
    }
    if (!ecs_world_has_t(world, load->textureAsset, AssetLoadedComp)) {
      goto Next; // Wait for the texture to be loaded.
    }
    if (UNLIKELY(!ecs_view_maybe_jump(textureItr, load->textureAsset))) {
      err = IconError_InvalidTexture;
      goto Error;
    }

    /**
     * Validate the icon texture.
     */
    const AssetTextureComp* texture = ecs_view_read_t(textureItr, AssetTextureComp);
    if (UNLIKELY(texture->width > icon_max_width || texture->height > icon_max_height)) {
      err = IconError_TextureTooBig;
      goto Error;
    }

    /**
     * Build icon.
     */
    AssetIconComp* icon = ecs_world_add_t(world, entity, AssetIconComp);
    asset_icon_generate(&load->def, texture, icon);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);

    asset_cache(world, entity, g_assetIconMeta, mem_create(icon, sizeof(AssetIconComp)));

    goto Cleanup;

  Error:
    log_e(
        "Failed to load icon",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error", fmt_text(icon_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetIconLoadComp);
    asset_release(world, load->textureAsset);

  Next:
    continue;
  }
}

/**
 * Remove any icon asset component for unloaded assets.
 */
ecs_system_define(UnloadIconAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetIconComp);
    ecs_utils_maybe_remove_t(world, entity, AssetIconSourceComp);
  }
}

ecs_module_init(asset_icon_module) {
  ecs_register_comp(AssetIconComp, .destructor = ecs_destruct_icon_comp);
  ecs_register_comp(AssetIconLoadComp, .destructor = ecs_destruct_icon_load_comp);
  ecs_register_comp(AssetIconSourceComp, .destructor = ecs_destruct_icon_source_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);
  ecs_register_view(UnloadView);

  ecs_register_system(
      LoadIconAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(TextureView));
  ecs_register_system(UnloadIconAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_icon(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, IconDef);
  data_reg_field_t(g_dataReg, IconDef, texture, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, IconDef, hotspotX, data_prim_t(u32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, IconDef, hotspotY, data_prim_t(u32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, IconDef, scale, data_prim_t(f32), .flags = DataFlags_NotEmpty | DataFlags_Opt);
  data_reg_field_t(g_dataReg, IconDef, color, g_assetGeoColor4Type, .container = DataContainer_Pointer, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetIconComp);
  data_reg_field_t(g_dataReg, AssetIconComp, width, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetIconComp, height, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetIconComp, hotspotX, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetIconComp, hotspotY, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetIconComp, pixelData, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  // clang-format on

  g_assetIconDefMeta = data_meta_t(t_IconDef);
  g_assetIconMeta    = data_meta_t(t_AssetIconComp);
}

void asset_load_icon(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  IconDef        iconDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_allocHeap, g_assetIconDefMeta, mem_var(iconDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetIconLoadComp, .def = iconDef);
  goto Cleanup;

Error:
  log_e(
      "Failed to load icon",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(errMsg)));
  data_destroy(g_dataReg, g_allocHeap, g_assetIconDefMeta, mem_var(iconDef));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}

void asset_load_icon_bin(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetIconComp  icon;
  DataReadResult result;
  data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetIconMeta, mem_var(icon), &result);

  if (UNLIKELY(result.error)) {
    log_e(
        "Failed to load binary icon",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error-code", fmt_int(result.error)),
        log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  *ecs_world_add_t(world, entity, AssetIconComp) = icon;
  ecs_world_add_t(world, entity, AssetIconSourceComp, .src = src);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}
