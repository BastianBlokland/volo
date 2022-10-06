#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataVfxDefMeta;

typedef struct {
  f32 r, g, b, a;
} AssetVfxColorDef;

typedef struct {
  String           atlasEntry;
  AssetVfxColorDef color;
} AssetVfxDef;

static void vfx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AssetVfxColorDef);
    data_reg_field_t(g_dataReg, AssetVfxColorDef, r, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxColorDef, g, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxColorDef, b, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxColorDef, a, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetVfxDef);
    data_reg_field_t(g_dataReg, AssetVfxDef, atlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetVfxDef, color, t_AssetVfxColorDef);
    // clang-format on

    g_dataVfxDefMeta = data_meta_t(t_AssetVfxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetVfxComp);

ecs_view_define(VfxUnloadView) {
  ecs_access_with(AssetVfxComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any vfx-asset components for unloaded assets.
 */
ecs_system_define(VfxUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, VfxUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetVfxComp);
  }
}

static GeoColor asset_vfx_build_color(const AssetVfxColorDef* def) {
  return geo_color(def->r, def->g, def->b, def->a);
}

static void asset_vfx_build(const AssetVfxDef* def, AssetVfxComp* out) {
  out->atlasEntry = string_hash(def->atlasEntry);
  out->color      = asset_vfx_build_color(&def->color);
}

ecs_module_init(asset_vfx_module) {
  vfx_datareg_init();

  ecs_register_comp(AssetVfxComp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

void asset_load_vfx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  AssetVfxDef    vfxDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataVfxDefMeta, mem_var(vfxDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  AssetVfxComp* result = ecs_world_add_t(world, entity, AssetVfxComp);
  asset_vfx_build(&vfxDef, result);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Vfx", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_alloc_heap, g_dataVfxDefMeta, mem_var(vfxDef));
  asset_repo_source_close(src);
}
