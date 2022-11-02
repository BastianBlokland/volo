#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "data_treescheme.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_read.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataNodeMeta;

typedef struct sAssetAiNodeDef AssetAiNodeDef;

typedef struct {
  const AssetAiNodeDef* values;
  usize                 count;
} AssetAiNodeDefList;

typedef struct {
  const AssetAiNodeDef* child;
} AssetAiNodeDefInvert;

typedef struct {
  const AssetAiNodeDef* child;
} AssetAiNodeDefTry;

typedef struct {
  const AssetAiNodeDef* child;
} AssetAiNodeDefRepeat;

typedef struct {
  AssetAiNodeDefList children;
} AssetAiNodeDefParallel;

typedef struct {
  AssetAiNodeDefList children;
} AssetAiNodeDefSelector;

typedef struct {
  AssetAiNodeDefList children;
} AssetAiNodeDefSequence;

typedef struct {
  String script;
} AssetAiNodeDefCondition;

typedef struct {
  String script;
} AssetAiNodeDefExecute;

typedef struct sAssetAiNodeDef {
  AssetAiNodeType type;
  String          name;
  union {
    AssetAiNodeDefInvert    data_invert;
    AssetAiNodeDefTry       data_try;
    AssetAiNodeDefRepeat    data_repeat;
    AssetAiNodeDefParallel  data_parallel;
    AssetAiNodeDefSelector  data_selector;
    AssetAiNodeDefSequence  data_sequence;
    AssetAiNodeDefCondition data_condition;
    AssetAiNodeDefExecute   data_execute;
  };
} AssetAiNodeDef;

static void behavior_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    const DataType nodeType = data_declare_t(g_dataReg, AssetAiNodeDef);

    data_reg_struct_t(g_dataReg, AssetAiNodeDefInvert);
    data_reg_field_t(g_dataReg, AssetAiNodeDefInvert, child, nodeType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefInvert, "Evaluates the child node and inverts its result.\nEvaluates to 'Running' if the child evaluates to 'Running', 'Success' if the child evaluated to 'Failure', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefTry);
    data_reg_field_t(g_dataReg, AssetAiNodeDefTry, child, nodeType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefTry, "Evaluates the child node.\nEvaluates to 'Running' if the child evaluates to 'Failure' or 'Running', otherwise to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefRepeat);
    data_reg_field_t(g_dataReg, AssetAiNodeDefRepeat, child, nodeType, .container = DataContainer_Pointer);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefRepeat, "Evaluates the child node.\nEvaluates to 'Running' if the child evaluates to 'Success' or 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefParallel);
    data_reg_field_t(g_dataReg, AssetAiNodeDefParallel, children, nodeType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefParallel, "Evaluates all children.\nEvaluates to 'Success' if any child evaluated to 'Success', 'Running' if any child evaluates to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefSelector);
    data_reg_field_t(g_dataReg, AssetAiNodeDefSelector, children, nodeType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefSelector, "Evaluates children until a child evaluates to 'Running' or 'Success'.\nEvaluates to 'Success' if any child evaluated to 'Success', 'Running' if any child evaluated to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefSequence);
    data_reg_field_t(g_dataReg, AssetAiNodeDefSequence, children, nodeType, .container = DataContainer_Array);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefSequence, "Evaluates children until a child evaluates to 'Failure'.\nEvaluates to 'Success' if all children evaluated to 'Success', 'Running' if any child evaluated to 'Running', otherwise to 'Failure'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefCondition);
    data_reg_field_t(g_dataReg, AssetAiNodeDefCondition, script, data_prim_t(String), .flags = DataFlags_HideName);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefCondition, "Evaluate the script condition.\nEvaluates to 'Success' when the script condition is truthy or 'Failure' if its not.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefExecute);
    data_reg_field_t(g_dataReg, AssetAiNodeDefExecute, script, data_prim_t(String), .flags = DataFlags_HideName);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefExecute, "Execute the script expression.\nEvaluates to 'Success'.");

    data_reg_union_t(g_dataReg, AssetAiNodeDef, type);
    data_reg_union_name_t(g_dataReg, AssetAiNodeDef, name);
    data_reg_choice_empty(g_dataReg, AssetAiNodeDef, AssetAiNode_Running);
    data_reg_choice_empty(g_dataReg, AssetAiNodeDef, AssetAiNode_Success);
    data_reg_choice_empty(g_dataReg, AssetAiNodeDef, AssetAiNode_Failure);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Invert, data_invert, t_AssetAiNodeDefInvert);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Try, data_try, t_AssetAiNodeDefTry);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Repeat, data_repeat, t_AssetAiNodeDefRepeat);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Parallel, data_parallel, t_AssetAiNodeDefParallel);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Selector, data_selector, t_AssetAiNodeDefSelector);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Sequence, data_sequence, t_AssetAiNodeDefSequence);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Condition, data_condition, t_AssetAiNodeDefCondition);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_Execute, data_execute, t_AssetAiNodeDefExecute);
    // clang-format on

    g_dataNodeMeta = data_meta_t(nodeType);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  BehaviorError_None              = 0,
  BehaviorError_ScriptInvalid     = 1,
  BehaviorError_ScriptNotReadonly = 2,

  BehaviorError_Count,
} BehaviorError;

static String behavior_error_str(const BehaviorError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Invalid script expression"),
      string_static("Script expression is not readonly"),
  };
  ASSERT(array_elems(g_msgs) == BehaviorError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

typedef struct {
  DynArray*     nodes;     // AssetAiNode[]
  DynArray*     nodeNames; // String[]
  ScriptDoc*    scriptDoc;
  BehaviorError error;
} BuildContext;

static AssetAiNodeId build_node(BuildContext*, const AssetAiNodeDef*);

static AssetAiNodeId build_node_id_peek(const BuildContext* ctx) {
  diag_assert(dynarray_size(ctx->nodes) < u16_max);
  return (AssetAiNodeId)dynarray_size(ctx->nodes);
}

static AssetAiNodeId build_node_list(BuildContext* ctx, const AssetAiNodeDefList* list) {
  const AssetAiNodeId beginId       = build_node_id_peek(ctx);
  AssetAiNodeId       prevSiblingId = beginId;
  for (u32 i = 0; i != list->count; ++i) {
    const AssetAiNodeId id = build_node(ctx, &list->values[i]);
    if (i) {
      // Insert it in the sibling linked-list.
      dynarray_at_t(ctx->nodes, prevSiblingId, AssetAiNode)->nextSibling = id;
    }
    prevSiblingId = id;
  }
  return list->count ? beginId : sentinel_u16;
}

static AssetAiNodeInvert build_node_invert(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeInvert){.child = build_node(ctx, def->data_invert.child)};
}

static AssetAiNodeTry build_node_try(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeTry){.child = build_node(ctx, def->data_try.child)};
}

static AssetAiNodeRepeat build_node_repeat(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeRepeat){.child = build_node(ctx, def->data_repeat.child)};
}

static AssetAiNodeParallel build_node_parallel(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeParallel){.childrenBegin = build_node_list(ctx, &def->data_parallel.children)};
}

static AssetAiNodeSelector build_node_selector(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeSelector){.childrenBegin = build_node_list(ctx, &def->data_selector.children)};
}

static AssetAiNodeSequence build_node_sequence(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeSequence){.childrenBegin = build_node_list(ctx, &def->data_sequence.children)};
}

static AssetAiNodeCondition build_node_condition(BuildContext* ctx, const AssetAiNodeDef* def) {
  ScriptReadResult readRes;
  script_read_all(ctx->scriptDoc, def->data_condition.script, &readRes);

  if (UNLIKELY(readRes.type != ScriptResult_Success)) {
    log_e("Invalid condition script", log_param("error", script_error_fmt(readRes.error)));
    ctx->error = BehaviorError_ScriptInvalid;
    return (AssetAiNodeCondition){.scriptExpr = sentinel_u32};
  }
  if (UNLIKELY(!script_expr_readonly(ctx->scriptDoc, readRes.expr))) {
    ctx->error = BehaviorError_ScriptNotReadonly;
    return (AssetAiNodeCondition){.scriptExpr = sentinel_u32};
  }
  return (AssetAiNodeCondition){.scriptExpr = readRes.expr};
}

static AssetAiNodeExecute build_node_execute(BuildContext* ctx, const AssetAiNodeDef* def) {
  ScriptReadResult readRes;
  script_read_all(ctx->scriptDoc, def->data_condition.script, &readRes);

  if (UNLIKELY(readRes.type != ScriptResult_Success)) {
    log_e("Invalid execute script", log_param("error", script_error_fmt(readRes.error)));
    ctx->error = BehaviorError_ScriptInvalid;
    return (AssetAiNodeExecute){.scriptExpr = sentinel_u32};
  }
  return (AssetAiNodeExecute){.scriptExpr = readRes.expr};
}

static AssetAiNodeId build_node(BuildContext* ctx, const AssetAiNodeDef* def) {
  const AssetAiNodeId id = build_node_id_peek(ctx);

  AssetAiNode* resNode = dynarray_push_t(ctx->nodes, AssetAiNode);
  resNode->type        = def->type;
  resNode->nextSibling = sentinel_u16;

  *dynarray_push_t(ctx->nodeNames, String) = string_maybe_dup(g_alloc_heap, def->name);

  switch (def->type) {
  case AssetAiNode_Running:
  case AssetAiNode_Success:
  case AssetAiNode_Failure:
    break;
  case AssetAiNode_Invert:
    resNode->data_invert = build_node_invert(ctx, def);
    break;
  case AssetAiNode_Try:
    resNode->data_try = build_node_try(ctx, def);
    break;
  case AssetAiNode_Repeat:
    resNode->data_repeat = build_node_repeat(ctx, def);
    break;
  case AssetAiNode_Parallel:
    resNode->data_parallel = build_node_parallel(ctx, def);
    break;
  case AssetAiNode_Selector:
    resNode->data_selector = build_node_selector(ctx, def);
    break;
  case AssetAiNode_Sequence:
    resNode->data_sequence = build_node_sequence(ctx, def);
    break;
  case AssetAiNode_Condition:
    resNode->data_condition = build_node_condition(ctx, def);
    break;
  case AssetAiNode_Execute:
    resNode->data_execute = build_node_execute(ctx, def);
    break;
  case AssetAiNode_Count:
    break;
  }
  return id;
}

ecs_comp_define_public(AssetBehaviorComp);

static void ecs_destruct_behavior_comp(void* data) {
  AssetBehaviorComp* comp = data;

  for (u32 i = 0; i != comp->nodeCount; ++i) {
    string_maybe_free(g_alloc_heap, comp->nodeNames[i]);
  }
  alloc_free_array_t(g_alloc_heap, comp->nodes, comp->nodeCount);
  alloc_free_array_t(g_alloc_heap, comp->nodeNames, comp->nodeCount);
  script_destroy((ScriptDoc*)comp->scriptDoc);
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

  DynArray   nodes     = dynarray_create_t(g_alloc_heap, AssetAiNode, 64);
  DynArray   nodeNames = dynarray_create_t(g_alloc_heap, String, 64);
  ScriptDoc* scriptDoc = script_create(g_alloc_heap);

  AssetAiNodeDef rootDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataNodeMeta, mem_var(rootDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  BuildContext ctx = {
      .nodes     = &nodes,
      .nodeNames = &nodeNames,
      .scriptDoc = scriptDoc,
  };
  build_node(&ctx, &rootDef);

  if (UNLIKELY(ctx.error)) {
    errMsg = behavior_error_str(ctx.error);
    goto Error;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetBehaviorComp,
      .nodes     = dynarray_copy_as_new(&nodes, g_alloc_heap),
      .nodeNames = dynarray_copy_as_new(&nodeNames, g_alloc_heap),
      .nodeCount = (u16)nodes.size,
      .scriptDoc = scriptDoc);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Behavior", log_param("error", fmt_text(errMsg)));
  script_destroy(scriptDoc);
  dynarray_for_t(&nodeNames, String, nodeName) { string_maybe_free(g_alloc_heap, *nodeName); }
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
  dynarray_destroy(&nodes);
  dynarray_destroy(&nodeNames);
  data_destroy(g_dataReg, g_alloc_heap, g_dataNodeMeta, mem_var(rootDef));
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
      string_static("Condition"),
      string_static("Execute"),
  };
  ASSERT(array_elems(g_names) == AssetAiNode_Count, "Incorrect number of names");
  return g_names[type];
}

void asset_behavior_scheme_write(DynString* str) {
  behavior_datareg_init();
  data_treescheme_write(g_dataReg, str, g_dataNodeMeta.type);
}
