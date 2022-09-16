#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "data.h"
#include "data_treescheme.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataBehaviorMeta;

static void behavior_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    const DataType behaviorType = data_declare_t(g_dataReg, AssetBehavior);

    data_reg_struct_t(g_dataReg, GeoVector);
    data_reg_field_t(g_dataReg, GeoVector, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GeoVector, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GeoVector, z, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GeoVector, w, data_prim_t(f32));

    data_reg_union_t(g_dataReg, AssetKnowledgeLit, type);
    data_reg_choice_t(g_dataReg, AssetKnowledgeLit, AssetKnowledgeType_f64, data_f64, data_prim_t(f64));
    data_reg_choice_t(g_dataReg, AssetKnowledgeLit, AssetKnowledgeType_Vector, data_vector, t_GeoVector);

    data_reg_struct_t(g_dataReg, AssetBehaviorInvert);
    data_reg_field_t(g_dataReg, AssetBehaviorInvert, child, behaviorType, .container = DataContainer_Pointer);

    data_reg_struct_t(g_dataReg, AssetBehaviorParallel);
    data_reg_field_t(g_dataReg, AssetBehaviorParallel, children, behaviorType, .container = DataContainer_Array);

    data_reg_struct_t(g_dataReg, AssetBehaviorSelector);
    data_reg_field_t(g_dataReg, AssetBehaviorSelector, children, behaviorType, .container = DataContainer_Array);

    data_reg_struct_t(g_dataReg, AssetBehaviorSequence);
    data_reg_field_t(g_dataReg, AssetBehaviorSequence, children, behaviorType, .container = DataContainer_Array);

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeSet);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeSet, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeSet, value, t_AssetKnowledgeLit);

    data_reg_union_t(g_dataReg, AssetBehavior, type);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehaviorType_Success);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehaviorType_Failure);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehaviorType_Invert, data_invert, t_AssetBehaviorInvert);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehaviorType_Parallel, data_parallel, t_AssetBehaviorParallel);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehaviorType_Selector, data_selector, t_AssetBehaviorSelector);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehaviorType_Sequence, data_sequence, t_AssetBehaviorSequence);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehaviorType_KnowledgeSet, data_knowledgeset, t_AssetBehaviorKnowledgeSet);
    // clang-format on

    g_dataBehaviorMeta = data_meta_t(behaviorType);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetBehaviorComp);

static void ecs_destruct_behavior_comp(void* data) {
  AssetBehaviorComp* comp            = data;
  const Mem          rootBehaviorMem = mem_create(&comp->root, sizeof(AssetBehavior));
  data_destroy(g_dataReg, g_alloc_heap, g_dataBehaviorMeta, rootBehaviorMem);
}

ecs_view_define(BehaviorUnloadView) {
  ecs_access_with(AssetBehaviorComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any behavior-asset component for unloaded assets.
 */
ecs_system_define(BehaviorUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, BehaviorUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetBehaviorComp);
  }
}

ecs_module_init(asset_behavior_module) {
  behavior_datareg_init();

  ecs_register_comp(AssetBehaviorComp, .destructor = ecs_destruct_behavior_comp);

  ecs_register_view(BehaviorUnloadView);

  ecs_register_system(BehaviorUnloadAssetSys, ecs_view_id(BehaviorUnloadView));
}

void asset_load_bt(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  AssetBehavior  root;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataBehaviorMeta, mem_var(root), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetBehaviorComp, .root = root);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Behavior", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}

void asset_behavior_scheme_write(DynString* str) {
  behavior_datareg_init();
  data_treescheme_write(g_dataReg, str, g_dataBehaviorMeta.type);
}
