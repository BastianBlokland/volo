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

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceNumber);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceNumber, value, data_prim_t(f64));

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceVector);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, z, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, w, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceKnowledge);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceKnowledge, key, data_prim_t(String));

    data_reg_union_t(g_dataReg, AssetKnowledgeSource, type);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Number, data_number, t_AssetKnowledgeSourceNumber);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Vector, data_vector, t_AssetKnowledgeSourceVector);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Knowledge, data_knowledge, t_AssetKnowledgeSourceKnowledge);

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
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeSet, value, t_AssetKnowledgeSource);

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeClear);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeClear, key, data_prim_t(String));

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeCheck);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeCheck, key, data_prim_t(String));

    data_reg_union_t(g_dataReg, AssetBehavior, type);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehavior_Success);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehavior_Failure);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Invert, data_invert, t_AssetBehaviorInvert);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Parallel, data_parallel, t_AssetBehaviorParallel);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Selector, data_selector, t_AssetBehaviorSelector);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Sequence, data_sequence, t_AssetBehaviorSequence);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeSet, data_knowledgeset, t_AssetBehaviorKnowledgeSet);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeClear, data_knowledgeclear, t_AssetBehaviorKnowledgeClear);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeCheck, data_knowledgecheck, t_AssetBehaviorKnowledgeCheck);
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
