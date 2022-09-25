#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_array.h"
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

    data_reg_enum_t(g_dataReg, AssetKnowledgeComparison);
    data_reg_const_t(g_dataReg, AssetKnowledgeComparison, Equal);
    data_reg_const_t(g_dataReg, AssetKnowledgeComparison, Less);

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceNumber);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceNumber, value, data_prim_t(f64));

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceBool);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceBool, value, data_prim_t(bool));

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceVector);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, z, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceVector, w, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetKnowledgeSourceKnowledge);
    data_reg_field_t(g_dataReg, AssetKnowledgeSourceKnowledge, key, data_prim_t(String));

    data_reg_union_t(g_dataReg, AssetKnowledgeSource, type);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Number, data_number, t_AssetKnowledgeSourceNumber);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Bool, data_bool, t_AssetKnowledgeSourceBool);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Vector, data_vector, t_AssetKnowledgeSourceVector);
    data_reg_choice_t(g_dataReg, AssetKnowledgeSource, AssetKnowledgeSource_Knowledge, data_knowledge, t_AssetKnowledgeSourceKnowledge);

    data_reg_struct_t(g_dataReg, AssetBehaviorInvert);
    data_reg_field_t(g_dataReg, AssetBehaviorInvert, child, behaviorType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetBehaviorInvert, "Evaluates the child node and inverts its result.\nEvaluates to 'Running' if the child evaluates to 'Running', 'Success' if the child evaluated to 'Failure', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorTry);
    data_reg_field_t(g_dataReg, AssetBehaviorTry, child, behaviorType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetBehaviorTry, "Evaluates the child node.\nEvaluates to 'Running' if the child evaluates to 'Failure' or 'Running', otherwise to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorParallel);
    data_reg_field_t(g_dataReg, AssetBehaviorParallel, children, behaviorType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetBehaviorParallel, "Evaluates all children.\nEvaluates to 'Success' if any child evaluated to 'Success', 'Running' if any child evaluates to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorSelector);
    data_reg_field_t(g_dataReg, AssetBehaviorSelector, children, behaviorType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetBehaviorSelector, "Evaluates children until a child evaluates to 'Running' or 'Success'.\nEvaluates to 'Success' if any child evaluated to 'Success', 'Running' if any child evaluated to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorSequence);
    data_reg_field_t(g_dataReg, AssetBehaviorSequence, children, behaviorType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetBehaviorSequence, "Evaluates children until a child evaluates to 'Failure'.\nEvaluates to 'Success' if all children evaluated to 'Success', 'Running' if any child evaluated to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeSet);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeSet, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeSet, value, t_AssetKnowledgeSource);
    data_reg_comment_t(g_dataReg, AssetBehaviorKnowledgeSet, "Assign knowledge to the given key.\nNote: Knowledge will be added if it does not exist.\nEvaluates to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeClear);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeClear, keys, data_prim_t(String), .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetBehaviorKnowledgeClear, "Clear all the given knowledge keys.\nEvaluates to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeCheck);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeCheck, keys, data_prim_t(String), .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetBehaviorKnowledgeCheck, "Check if knowledge exists for all the given keys.\nEvaluates to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetBehaviorKnowledgeCompare);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeCompare, comparison, t_AssetKnowledgeComparison);
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeCompare, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetBehaviorKnowledgeCompare, value, t_AssetKnowledgeSource);
    data_reg_comment_t(g_dataReg, AssetBehaviorKnowledgeCompare, "Compare the knowledge value at the given key to a value source.\nEvaluates to 'Success' or 'Failure'.");

    data_reg_union_t(g_dataReg, AssetBehavior, type);
    data_reg_union_name_t(g_dataReg, AssetBehavior, name);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehavior_Running);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehavior_Success);
    data_reg_choice_empty(g_dataReg, AssetBehavior, AssetBehavior_Failure);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Invert, data_invert, t_AssetBehaviorInvert);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Try, data_try, t_AssetBehaviorTry);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Parallel, data_parallel, t_AssetBehaviorParallel);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Selector, data_selector, t_AssetBehaviorSelector);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_Sequence, data_sequence, t_AssetBehaviorSequence);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeSet, data_knowledgeset, t_AssetBehaviorKnowledgeSet);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeClear, data_knowledgeclear, t_AssetBehaviorKnowledgeClear);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeCheck, data_knowledgecheck, t_AssetBehaviorKnowledgeCheck);
    data_reg_choice_t(g_dataReg, AssetBehavior, AssetBehavior_KnowledgeCompare, data_knowledgecompare, t_AssetBehaviorKnowledgeCompare);
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

String asset_behavior_type_str(const AssetBehaviorType type) {
  static const String g_names[] = {
      string_static("Running"),
      string_static("Success"),
      string_static("Failure"),
      string_static("Invert"),
      string_static("Try"),
      string_static("Parallel"),
      string_static("Selector"),
      string_static("Sequence"),
      string_static("KnowledgeSet"),
      string_static("KnowledgeClear"),
      string_static("KnowledgeCheck"),
      string_static("KnowledgeCompare"),
  };
  ASSERT(array_elems(g_names) == AssetBehavior_Count, "Incorrect number of names");
  return g_names[type];
}

void asset_behavior_scheme_write(DynString* str) {
  behavior_datareg_init();
  data_treescheme_write(g_dataReg, str, g_dataBehaviorMeta.type);
}
