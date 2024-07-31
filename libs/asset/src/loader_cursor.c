#include "asset_cursor.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "data.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetCursorDataDef;

typedef struct {
  String    texture;
  u32       hotspotX, hotspotY;
  f32       scale;
  GeoColor* color;
} CursorDef;

typedef enum {
  CursorError_None,
  CursorError_InvalidTexture,

  CursorError_Count,
} CursorError;

static String cursor_error_str(const CursorError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Cursor specifies an invalid texture"),
  };
  ASSERT(array_elems(g_msgs) == CursorError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

ecs_comp_define_public(AssetCursorComp);

ecs_comp_define(AssetCursorLoadComp) {
  CursorDef   def;
  EcsEntityId textureAsset;
};

static void ecs_destruct_cursor_comp(void* data) {
  AssetCursorComp* comp = data;
  if (comp->pixels) {
    alloc_free_array_t(g_allocHeap, comp->pixels, comp->width * comp->height);
  }
}

static void ecs_destruct_cursor_load_comp(void* data) {
  AssetCursorLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetCursorDataDef, mem_var(comp->def));
}

static AssetCursorPixel asset_cursor_pixel(const GeoColor color) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;
  return (AssetCursorPixel){
      .r = (u8)(color.r * g_u8MaxPlusOneRoundDown),
      .g = (u8)(color.g * g_u8MaxPlusOneRoundDown),
      .b = (u8)(color.b * g_u8MaxPlusOneRoundDown),
      .a = (u8)(color.a * g_u8MaxPlusOneRoundDown),
  };
}

static void asset_cursor_generate(
    const CursorDef* def, const AssetTextureComp* texture, AssetCursorComp* outCursor) {

  const f32 scale     = def->scale < f32_epsilon ? 1.0f : def->scale;
  const u32 outWidth  = math_max((u32)math_round_nearest_f32(texture->width * scale), 1);
  const u32 outHeight = math_max((u32)math_round_nearest_f32(texture->height * scale), 1);

  const u32   pixelCount   = outWidth * outHeight;
  const usize pixelMemSize = sizeof(AssetCursorPixel) * pixelCount;
  const Mem   pixelMem     = alloc_alloc(g_allocHeap, pixelMemSize, sizeof(AssetCursorPixel));

  const bool        colored      = def->color != null;
  const GeoColor    colorMul     = def->color ? *def->color : geo_color_white;
  const f32         outWidthInv  = 1.0f / (f32)outWidth;
  const f32         outHeightInv = 1.0f / (f32)outHeight;
  AssetCursorPixel* outPixels    = pixelMem.ptr;
  for (u32 y = 0; y != outHeight; ++y) {
    const f32 yNorm = (f32)(y + 0.5f) * outHeightInv;
    for (u32 x = 0; x != outWidth; ++x) {
      const f32 xNorm = (f32)(x + 0.5f) * outWidthInv;

      const u32 layer       = 0;
      GeoColor  colorLinear = asset_texture_sample(texture, xNorm, yNorm, layer);

      if (colored) {
        colorLinear = geo_color_mul_comps(colorLinear, colorMul);
        colorLinear = geo_color_clamp_comps(colorLinear, geo_color_clear, geo_color_white);
      }

      // Always output Srgb encoded pixels.
      outPixels[y * outWidth + x] = asset_cursor_pixel(geo_color_linear_to_srgb(colorLinear));
    }
  }

  outCursor->width    = outWidth;
  outCursor->height   = outHeight;
  outCursor->hotspotX = math_min((u32)math_round_nearest_f32(def->hotspotX * scale), outWidth - 1);
  outCursor->hotspotY = math_min((u32)math_round_nearest_f32(def->hotspotY * scale), outHeight - 1);
  outCursor->pixels   = pixelMem.ptr;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_write(AssetCursorLoadComp);
}

ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

ecs_view_define(UnloadView) {
  ecs_access_with(AssetCursorComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load cursor assets.
 */
ecs_system_define(LoadCursorAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }

  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    const String         id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetCursorLoadComp* load   = ecs_view_write_t(itr, AssetCursorLoadComp);
    CursorError          err;

    /**
     * Start loading the cursor texture.
     */
    if (!load->textureAsset) {
      load->textureAsset = asset_lookup(world, manager, load->def.texture);
      asset_acquire(world, load->textureAsset);
      asset_register_dep(world, entity, load->textureAsset);
    }

    /**
     * Wait for the cursor texture.
     */
    if (ecs_world_has_t(world, load->textureAsset, AssetFailedComp)) {
      err = CursorError_InvalidTexture;
      goto Error;
    }
    if (!ecs_world_has_t(world, load->textureAsset, AssetLoadedComp)) {
      goto Next; // Wait for the texture to be loaded.
    }
    if (UNLIKELY(!ecs_view_maybe_jump(textureItr, load->textureAsset))) {
      err = CursorError_InvalidTexture;
      goto Error;
    }

    /**
     * Build cursor.
     */
    const AssetTextureComp* texture = ecs_view_read_t(textureItr, AssetTextureComp);
    AssetCursorComp*        cursor  = ecs_world_add_t(world, entity, AssetCursorComp);
    asset_cursor_generate(&load->def, texture, cursor);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e(
        "Failed to load cursor",
        log_param("id", fmt_text(id)),
        log_param("error", fmt_text(cursor_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetCursorLoadComp);
    asset_release(world, load->textureAsset);

  Next:
    continue;
  }
}

/**
 * Remove any cursor asset component for unloaded assets.
 */
ecs_system_define(UnloadCursorAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetCursorComp);
  }
}

ecs_module_init(asset_cursor_module) {
  ecs_register_comp(AssetCursorComp, .destructor = ecs_destruct_cursor_comp);
  ecs_register_comp(AssetCursorLoadComp, .destructor = ecs_destruct_cursor_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);
  ecs_register_view(UnloadView);

  ecs_register_system(
      LoadCursorAssetSys,
      ecs_view_id(ManagerView),
      ecs_view_id(LoadView),
      ecs_view_id(TextureView));
  ecs_register_system(UnloadCursorAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_cursor(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, CursorDef);
  data_reg_field_t(g_dataReg, CursorDef, texture, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, CursorDef, hotspotX, data_prim_t(u32));
  data_reg_field_t(g_dataReg, CursorDef, hotspotY, data_prim_t(u32));
  data_reg_field_t(g_dataReg, CursorDef, scale, data_prim_t(f32), .flags = DataFlags_NotEmpty | DataFlags_Opt);
  data_reg_field_t(g_dataReg, CursorDef, color, g_assetGeoColorType, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
  // clang-format on

  g_assetCursorDataDef = data_meta_t(t_CursorDef);
}

void asset_load_cursor(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  CursorDef      cursorDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(
      g_dataReg, src->data, g_allocHeap, g_assetCursorDataDef, mem_var(cursorDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetCursorLoadComp, .def = cursorDef);
  goto Cleanup;

Error:
  log_e(
      "Failed to load cursor", log_param("id", fmt_text(id)), log_param("error", fmt_text(errMsg)));
  data_destroy(g_dataReg, g_allocHeap, g_assetCursorDataDef, mem_var(cursorDef));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}
