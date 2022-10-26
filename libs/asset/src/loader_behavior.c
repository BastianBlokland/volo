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
static DataMeta g_dataNodeMeta;

static void behavior_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    const DataType nodeType = data_declare_t(g_dataReg, AssetAiNode);

    data_reg_enum_t(g_dataReg, AssetAiComparison);
    data_reg_const_t(g_dataReg, AssetAiComparison, Equal);
    data_reg_const_t(g_dataReg, AssetAiComparison, NotEqual);
    data_reg_const_t(g_dataReg, AssetAiComparison, Less);
    data_reg_const_t(g_dataReg, AssetAiComparison, LessOrEqual);
    data_reg_const_t(g_dataReg, AssetAiComparison, Greater);
    data_reg_const_t(g_dataReg, AssetAiComparison, GreaterOrEqual);

    data_reg_struct_t(g_dataReg, AssetAiSourceNumber);
    data_reg_field_t(g_dataReg, AssetAiSourceNumber, value, data_prim_t(f64));

    data_reg_struct_t(g_dataReg, AssetAiSourceBool);
    data_reg_field_t(g_dataReg, AssetAiSourceBool, value, data_prim_t(bool));

    data_reg_struct_t(g_dataReg, AssetAiSourceVector);
    data_reg_field_t(g_dataReg, AssetAiSourceVector, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetAiSourceVector, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetAiSourceVector, z, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetAiSourceTime);
    data_reg_field_t(g_dataReg, AssetAiSourceTime, secondsFromNow, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetAiSourceKnowledge);
    data_reg_field_t(g_dataReg, AssetAiSourceKnowledge, key, data_prim_t(String));

    data_reg_union_t(g_dataReg, AssetAiSource, type);
    data_reg_choice_empty(g_dataReg, AssetAiSource, AssetAiSource_None);
    data_reg_choice_t(g_dataReg, AssetAiSource, AssetAiSource_Number, data_number, t_AssetAiSourceNumber);
    data_reg_choice_t(g_dataReg, AssetAiSource, AssetAiSource_Bool, data_bool, t_AssetAiSourceBool);
    data_reg_choice_t(g_dataReg, AssetAiSource, AssetAiSource_Vector, data_vector, t_AssetAiSourceVector);
    data_reg_choice_t(g_dataReg, AssetAiSource, AssetAiSource_Time, data_time, t_AssetAiSourceTime);
    data_reg_choice_t(g_dataReg, AssetAiSource, AssetAiSource_Knowledge, data_knowledge, t_AssetAiSourceKnowledge);

    data_reg_struct_t(g_dataReg, AssetAiNodeInvert);
    data_reg_field_t(g_dataReg, AssetAiNodeInvert, child, nodeType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetAiNodeInvert, "Evaluates the child node and inverts its result.\nEvaluates to 'Running' if the child evaluates to 'Running', 'Success' if the child evaluated to 'Failure', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeTry);
    data_reg_field_t(g_dataReg, AssetAiNodeTry, child, nodeType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetAiNodeTry, "Evaluates the child node.\nEvaluates to 'Running' if the child evaluates to 'Failure' or 'Running', otherwise to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeRepeat);
    data_reg_field_t(g_dataReg, AssetAiNodeRepeat, child, nodeType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetAiNodeRepeat, "Evaluates the child node.\nEvaluates to 'Running' if the child evaluates to 'Success' or 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeParallel);
    data_reg_field_t(g_dataReg, AssetAiNodeParallel, children, nodeType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetAiNodeParallel, "Evaluates all children.\nEvaluates to 'Success' if any child evaluated to 'Success', 'Running' if any child evaluates to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeSelector);
    data_reg_field_t(g_dataReg, AssetAiNodeSelector, children, nodeType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetAiNodeSelector, "Evaluates children until a child evaluates to 'Running' or 'Success'.\nEvaluates to 'Success' if any child evaluated to 'Success', 'Running' if any child evaluated to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeSequence);
    data_reg_field_t(g_dataReg, AssetAiNodeSequence, children, nodeType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetAiNodeSequence, "Evaluates children until a child evaluates to 'Failure'.\nEvaluates to 'Success' if all children evaluated to 'Success', 'Running' if any child evaluated to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeKnowledgeSet);
    data_reg_field_t(g_dataReg, AssetAiNodeKnowledgeSet, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetAiNodeKnowledgeSet, value, t_AssetAiSource, .flags = DataFlags_Opt);
    data_reg_comment_t(g_dataReg, AssetAiNodeKnowledgeSet, "Assign knowledge to the given key.\nNote: Knowledge will be added if it does not exist.\nEvaluates to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeKnowledgeCompare);
    data_reg_field_t(g_dataReg, AssetAiNodeKnowledgeCompare, comparison, t_AssetAiComparison);
    data_reg_field_t(g_dataReg, AssetAiNodeKnowledgeCompare, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetAiNodeKnowledgeCompare, value, t_AssetAiSource, .flags = DataFlags_Opt);
    data_reg_comment_t(g_dataReg, AssetAiNodeKnowledgeCompare, "Compare the knowledge value at the given key to a value source.\nEvaluates to 'Success' or 'Failure'.");

    data_reg_union_t(g_dataReg, AssetAiNode, type);
    data_reg_union_name_t(g_dataReg, AssetAiNode, name);
    data_reg_choice_empty(g_dataReg, AssetAiNode, AssetAiNode_Running);
    data_reg_choice_empty(g_dataReg, AssetAiNode, AssetAiNode_Success);
    data_reg_choice_empty(g_dataReg, AssetAiNode, AssetAiNode_Failure);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_Invert, data_invert, t_AssetAiNodeInvert);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_Try, data_try, t_AssetAiNodeTry);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_Repeat, data_repeat, t_AssetAiNodeRepeat);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_Parallel, data_parallel, t_AssetAiNodeParallel);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_Selector, data_selector, t_AssetAiNodeSelector);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_Sequence, data_sequence, t_AssetAiNodeSequence);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_KnowledgeSet, data_knowledgeset, t_AssetAiNodeKnowledgeSet);
    data_reg_choice_t(g_dataReg, AssetAiNode, AssetAiNode_KnowledgeCompare, data_knowledgecompare, t_AssetAiNodeKnowledgeCompare);
    // clang-format on

    g_dataNodeMeta = data_meta_t(nodeType);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetBehaviorComp);

static void ecs_destruct_behavior_comp(void* data) {
  AssetBehaviorComp* comp        = data;
  const Mem          rootNodeMem = mem_create(&comp->root, sizeof(AssetAiNode));
  data_destroy(g_dataReg, g_alloc_heap, g_dataNodeMeta, rootNodeMem);
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

  AssetAiNode    root;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataNodeMeta, mem_var(root), &readRes);
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

String asset_behavior_type_str(const AssetAiNodeType type) {
  static const String g_names[] = {
      string_static("Running"),
      string_static("Success"),
      string_static("Failure"),
      string_static("Invert"),
      string_static("Try"),
      string_static("Repeat"),
      string_static("Parallel"),
      string_static("Selector"),
      string_static("Sequence"),
      string_static("KnowledgeSet"),
      string_static("KnowledgeCompare"),
  };
  ASSERT(array_elems(g_names) == AssetAiNode_Count, "Incorrect number of names");
  return g_names[type];
}

void asset_behavior_scheme_write(DynString* str) {
  behavior_datareg_init();
  data_treescheme_write(g_dataReg, str, g_dataNodeMeta.type);
}
