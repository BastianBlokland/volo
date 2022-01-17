#include "asset_font.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataFtxDefMeta;

typedef struct {
  String fontId;
  u32    textureSize;
} FtxDefinition;

static void ftx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    data_reg_struct_t(g_dataReg, FtxDefinition);
    data_reg_field_t(g_dataReg, FtxDefinition, fontId, data_prim_t(String));
    data_reg_field_t(g_dataReg, FtxDefinition, textureSize, data_prim_t(u32));

    g_dataFtxDefMeta = data_meta_t(t_FtxDefinition);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  FtxLoadState_FontAcquire,
  FtxLoadState_FontWait,
  FtxLoadState_Generate,
} FtxLoadState;

ecs_comp_define(AssetFtxLoadComp) {
  FtxDefinition def;
  FtxLoadState  state;
  EcsEntityId   font;
};

static void ecs_destruct_ftx_load_comp(void* data) {
  AssetFtxLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataFtxDefMeta, mem_var(comp->def));
}

typedef enum {
  FtxError_None = 0,
  FtxError_FontNotSpecified,
  FtxError_FontInvalid,
  FtxError_NonPow2TextureSize,

  FtxError_Count,
} FtxError;

static String ftx_error_str(const FtxError err) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Ftx definition does not specify a font"),
      string_static("Ftx definition specifies an invalid font"),
      string_static("Ftx definition specifies a non power-of-two texture size"),
  };
  ASSERT(array_elems(msgs) == FtxError_Count, "Incorrect number of ftx-error messages");
  return msgs[err];
}

static void ftx_load_fail(
    EcsWorld* world, const EcsEntityId entity, AssetFtxLoadComp* load, const FtxError err) {
  log_e("Failed to load Ftx font-texture", log_param("error", fmt_text(ftx_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  ecs_world_remove_t(world, entity, AssetFtxLoadComp);
  if (load->font) {
    asset_release(world, load->font);
  }
}

static bool ftx_font_acquire(
    EcsWorld* world, AssetManagerComp* manager, const EcsEntityId entity, AssetFtxLoadComp* load) {
  if (UNLIKELY(string_is_empty(load->def.fontId))) {
    ftx_load_fail(world, entity, load, FtxError_FontNotSpecified);
    return false;
  }
  load->font = asset_lookup(world, manager, load->def.fontId);
  asset_acquire(world, load->font);
  return true;
}

static bool ftx_font_wait(EcsWorld* world, const EcsEntityId entity, AssetFtxLoadComp* load) {
  if (ecs_world_has_t(world, load->font, AssetFailedComp)) {
    ftx_load_fail(world, entity, load, FtxError_FontInvalid);
    return false;
  }
  return ecs_world_has_t(world, load->font, AssetLoadedComp);
}

static AssetTexturePixel* ftx_generate_sdf(const FtxDefinition* def, const AssetFontComp* font) {
  const u32          pixelCount = def->textureSize * def->textureSize;
  AssetTexturePixel* pixels     = alloc_array_t(g_alloc_heap, AssetTexturePixel, pixelCount);

  (void)font;

  for (usize y = 0; y != def->textureSize; ++y) {
    for (usize x = 0; x != def->textureSize; ++x) {
      pixels[y * def->textureSize + x] = (AssetTexturePixel){255, 0, 0, 255};
    }
  }
  return pixels;
}

static bool
ftx_generate(EcsWorld* world, const EcsEntityId entity, AssetFtxLoadComp* load, EcsView* fontView) {

  if (UNLIKELY(!bits_ispow2(load->def.textureSize))) {
    ftx_load_fail(world, entity, load, FtxError_NonPow2TextureSize);
    return false;
  }

  EcsIterator* fontItr = ecs_view_maybe_at(fontView, load->font);
  if (UNLIKELY(!fontItr)) {
    ftx_load_fail(world, entity, load, FtxError_FontInvalid);
    return false;
  }
  const AssetFontComp* fontComp = ecs_view_read_t(fontItr, AssetFontComp);

  AssetTexturePixel* pixels = ftx_generate_sdf(&load->def, fontComp);
  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .width  = load->def.textureSize,
      .height = load->def.textureSize,
      .pixels = pixels);
  return true;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetFtxLoadComp); }
ecs_view_define(FontView) { ecs_access_write(AssetFontComp); }

/**
 * Update all active loads.
 */
ecs_system_define(FtxLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  EcsView* fontView = ecs_world_view_t(world, FontView);

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetFtxLoadComp* load   = ecs_view_write_t(itr, AssetFtxLoadComp);
    switch (load->state) {
    case FtxLoadState_FontAcquire:
      if (ftx_font_acquire(world, manager, entity, load)) {
        ++load->state;
      }
      break;
    case FtxLoadState_FontWait:
      if (ftx_font_wait(world, entity, load)) {
        ++load->state;
      }
      break;
    case FtxLoadState_Generate:
      if (ftx_generate(world, entity, load, fontView)) {
        asset_release(world, load->font);
        ecs_world_remove_t(world, entity, AssetFtxLoadComp);
        ecs_world_add_empty_t(world, entity, AssetLoadedComp);
      }
      break;
    }
  }
}

ecs_module_init(asset_ftx_module) {
  ftx_datareg_init();

  ecs_register_comp(AssetFtxLoadComp, .destructor = ecs_destruct_ftx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(FontView);

  ecs_register_system(
      FtxLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(FontView));
}

void asset_load_ftx(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  // Parse the definition.
  FtxDefinition  definition;
  DataReadResult result;
  data_read_json(
      g_dataReg, src->data, g_alloc_heap, g_dataFtxDefMeta, mem_var(definition), &result);
  if (result.error) {
    log_e("Failed to load Ftx definition", log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    goto Cleanup;
  }

  // Start the load process.
  ecs_world_add_t(world, entity, AssetFtxLoadComp, .def = definition);

Cleanup:
  asset_repo_source_close(src);
}
