#include "asset_inputmap.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataInputmapMeta;

static void inputmap_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AssetInputmapComp);
    // clang-format on

    g_dataInputmapMeta = data_meta_t(t_AssetInputmapComp);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetInputmapComp);

static void ecs_destruct_inputmap_comp(void* data) {
  AssetInputmapComp* comp = data;
  (void)comp;
}

ecs_view_define(InputmapUnloadView) {
  ecs_access_with(AssetInputmapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any inputmap-asset component for unloaded assets.
 */
ecs_system_define(InputmapUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, InputmapUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetInputmapComp);
  }
}

ecs_module_init(asset_inputmap_module) {
  inputmap_datareg_init();

  ecs_register_comp(AssetInputmapComp, .destructor = ecs_destruct_inputmap_comp);

  ecs_register_view(InputmapUnloadView);

  ecs_register_system(InputmapUnloadAssetSys, ecs_view_id(InputmapUnloadView));
}

void asset_load_imp(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  String            errMsg;
  AssetInputmapComp data;
  DataReadResult    result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataInputmapMeta, mem_var(data), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }

  *ecs_world_add_t(world, entity, AssetInputmapComp) = data;
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load Inputmap", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataInputmapMeta, mem_var(data));
  asset_repo_source_close(src);
}
