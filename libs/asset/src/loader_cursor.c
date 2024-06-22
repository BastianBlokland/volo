#include "asset_cursor.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_thread.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataCursorDefMeta;

typedef struct {
  String texture;
  u32    hotspotX, hotspotY;
} CursorDef;

static void cursor_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_allocPersist);

    data_reg_struct_t(reg, CursorDef);
    data_reg_field_t(reg, CursorDef, texture, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, CursorDef, hotspotX, data_prim_t(u32));
    data_reg_field_t(reg, CursorDef, hotspotY, data_prim_t(u32));

    g_dataCursorDefMeta = data_meta_t(t_CursorDef);
    g_dataReg           = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  CursorError_None                   = 0,
  CursorError_InvalidTexture         = 1,
  CursorError_InvalidTextureType     = 2,
  CursorError_InvalidTextureChannels = 3,
  CursorError_InvalidTextureLayers   = 4,

  CursorError_Count,
} CursorError;

static String cursor_error_str(const CursorError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Cursor specifies an invalid texture"),
      string_static("Cursor uses an unsupported pixel type (only u8 supported)"),
      string_static("Cursor uses an unsupported channel count (only 4 supported)"),
      string_static("Cursor uses an unsupported layer count (only 1 supported)"),
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
  data_destroy(g_dataReg, g_allocHeap, g_dataCursorDefMeta, mem_var(comp->def));
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetCursorLoadComp); }
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
     * Validate cursor texture.
     */
    const AssetTextureComp* texture = ecs_view_read_t(textureItr, AssetTextureComp);
    if (UNLIKELY(texture->type != AssetTextureType_U8)) {
      err = CursorError_InvalidTextureType;
      goto Error;
    }
    if (UNLIKELY(texture->channels != AssetTextureChannels_Four)) {
      err = CursorError_InvalidTextureChannels;
      goto Error;
    }
    if (UNLIKELY(texture->layers > 1)) {
      err = CursorError_InvalidTextureLayers;
      goto Error;
    }

    /**
     * Build cursor.
     */
    const u32   pixelCount   = texture->width * texture->height;
    const usize pixelMemSize = sizeof(AssetCursorPixel) * pixelCount;
    const Mem   pixelMem     = alloc_alloc(g_allocHeap, pixelMemSize, sizeof(AssetCursorPixel));
    mem_cpy(pixelMem, mem_create(texture->pixelsRaw, pixelMemSize));
    ecs_world_add_t(
        world,
        entity,
        AssetCursorComp,
        .width    = texture->width,
        .height   = texture->height,
        .hotspotX = load->def.hotspotX,
        .hotspotY = load->def.hotspotY,
        .pixels   = pixelMem.ptr);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load cursor", log_param("error", fmt_text(cursor_error_str(err))));
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
  cursor_datareg_init();

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

void asset_load_cursor(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  CursorDef      cursorDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(
      g_dataReg, src->data, g_allocHeap, g_dataCursorDefMeta, mem_var(cursorDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetCursorLoadComp, .def = cursorDef);
  goto Cleanup;

Error:
  log_e("Failed to load cursor", log_param("error", fmt_text(errMsg)));
  data_destroy(g_dataReg, g_allocHeap, g_dataCursorDefMeta, mem_var(cursorDef));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}

void asset_cursor_jsonschema_write(DynString* str) {
  cursor_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataCursorDefMeta, schemaFlags);
}
