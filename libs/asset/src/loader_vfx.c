#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataVfxCompMeta;

static void vfx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, GeoColor);
    data_reg_field_t(g_dataReg, GeoColor, r, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GeoColor, g, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GeoColor, b, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GeoColor, a, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetVfxComp);
    data_reg_field_t(g_dataReg, AssetVfxComp, atlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetVfxComp, color, t_GeoColor);
    // clang-format on

    g_dataVfxCompMeta = data_meta_t(t_AssetVfxComp);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetVfxComp);

static void ecs_destruct_vfx_comp(void* data) {
  AssetVfxComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->atlasEntry);
}

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

ecs_module_init(asset_vfx_module) {
  vfx_datareg_init();

  ecs_register_comp(AssetVfxComp, .destructor = ecs_destruct_vfx_comp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

void asset_load_vfx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  AssetVfxComp   vfxComp;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataVfxCompMeta, mem_var(vfxComp), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  *ecs_world_add_t(world, entity, AssetVfxComp) = vfxComp;

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Vfx", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}
