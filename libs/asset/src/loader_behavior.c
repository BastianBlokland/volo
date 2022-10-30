#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "data.h"
#include "data_treescheme.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataNodeMeta;

typedef struct sAssetAiNodeDef AssetAiNodeDef;

typedef struct {
  f64 value;
} AssetAiSourceDefNumber;

typedef struct {
  bool value;
} AssetAiSourceDefBool;

typedef struct {
  f32 x, y, z;
} AssetAiSourceDefVector;

typedef struct {
  f32 secondsFromNow;
} AssetAiSourceDefTime;

typedef struct {
  String key;
} AssetAiSourceDefKnowledge;

typedef struct {
  AssetAiSourceType type;
  union {
    AssetAiSourceDefNumber    data_number;
    AssetAiSourceDefBool      data_bool;
    AssetAiSourceDefVector    data_vector;
    AssetAiSourceDefTime      data_time;
    AssetAiSourceDefKnowledge data_knowledge;
  };
} AssetAiSourceDef;

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
  String           key;
  AssetAiSourceDef value;
} AssetAiNodeDefKnowledgeSet;

typedef struct {
  AssetAiComparison comparison;
  String            key;
  AssetAiSourceDef  value;
} AssetAiNodeDefKnowledgeCompare;

typedef struct sAssetAiNodeDef {
  AssetAiNodeType type;
  String          name;
  union {
    AssetAiNodeDefInvert           data_invert;
    AssetAiNodeDefTry              data_try;
    AssetAiNodeDefRepeat           data_repeat;
    AssetAiNodeDefParallel         data_parallel;
    AssetAiNodeDefSelector         data_selector;
    AssetAiNodeDefSequence         data_sequence;
    AssetAiNodeDefKnowledgeSet     data_knowledgeset;
    AssetAiNodeDefKnowledgeCompare data_knowledgecompare;
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

    data_reg_enum_t(g_dataReg, AssetAiComparison);
    data_reg_const_t(g_dataReg, AssetAiComparison, Equal);
    data_reg_const_t(g_dataReg, AssetAiComparison, NotEqual);
    data_reg_const_t(g_dataReg, AssetAiComparison, Less);
    data_reg_const_t(g_dataReg, AssetAiComparison, LessOrEqual);
    data_reg_const_t(g_dataReg, AssetAiComparison, Greater);
    data_reg_const_t(g_dataReg, AssetAiComparison, GreaterOrEqual);

    data_reg_struct_t(g_dataReg, AssetAiSourceDefNumber);
    data_reg_field_t(g_dataReg, AssetAiSourceDefNumber, value, data_prim_t(f64));

    data_reg_struct_t(g_dataReg, AssetAiSourceDefBool);
    data_reg_field_t(g_dataReg, AssetAiSourceDefBool, value, data_prim_t(bool));

    data_reg_struct_t(g_dataReg, AssetAiSourceDefVector);
    data_reg_field_t(g_dataReg, AssetAiSourceDefVector, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetAiSourceDefVector, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetAiSourceDefVector, z, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetAiSourceDefTime);
    data_reg_field_t(g_dataReg, AssetAiSourceDefTime, secondsFromNow, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetAiSourceDefKnowledge);
    data_reg_field_t(g_dataReg, AssetAiSourceDefKnowledge, key, data_prim_t(String));

    data_reg_union_t(g_dataReg, AssetAiSourceDef, type);
    data_reg_choice_empty(g_dataReg, AssetAiSourceDef, AssetAiSource_None);
    data_reg_choice_t(g_dataReg, AssetAiSourceDef, AssetAiSource_Number, data_number, t_AssetAiSourceDefNumber);
    data_reg_choice_t(g_dataReg, AssetAiSourceDef, AssetAiSource_Bool, data_bool, t_AssetAiSourceDefBool);
    data_reg_choice_t(g_dataReg, AssetAiSourceDef, AssetAiSource_Vector, data_vector, t_AssetAiSourceDefVector);
    data_reg_choice_t(g_dataReg, AssetAiSourceDef, AssetAiSource_Time, data_time, t_AssetAiSourceDefTime);
    data_reg_choice_t(g_dataReg, AssetAiSourceDef, AssetAiSource_Knowledge, data_knowledge, t_AssetAiSourceDefKnowledge);

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

    data_reg_struct_t(g_dataReg, AssetAiNodeDefKnowledgeSet);
    data_reg_field_t(g_dataReg, AssetAiNodeDefKnowledgeSet, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetAiNodeDefKnowledgeSet, value, t_AssetAiSourceDef, .flags = DataFlags_Opt);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefKnowledgeSet, "Assign knowledge to the given key.\nNote: Knowledge will be added if it does not exist.\nEvaluates to 'Success'.");

    data_reg_struct_t(g_dataReg, AssetAiNodeDefKnowledgeCompare);
    data_reg_field_t(g_dataReg, AssetAiNodeDefKnowledgeCompare, comparison, t_AssetAiComparison);
    data_reg_field_t(g_dataReg, AssetAiNodeDefKnowledgeCompare, key, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetAiNodeDefKnowledgeCompare, value, t_AssetAiSourceDef, .flags = DataFlags_Opt);
    data_reg_comment_t(g_dataReg, AssetAiNodeDefKnowledgeCompare, "Compare the knowledge value at the given key to a value source.\nEvaluates to 'Success' or 'Failure'.");

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
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_KnowledgeSet, data_knowledgeset, t_AssetAiNodeDefKnowledgeSet);
    data_reg_choice_t(g_dataReg, AssetAiNodeDef, AssetAiNode_KnowledgeCompare, data_knowledgecompare, t_AssetAiNodeDefKnowledgeCompare);
    // clang-format on

    g_dataNodeMeta = data_meta_t(nodeType);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef struct {
  StringTable* stringTable;
  DynArray*    nodes;     // AssetAiNode[]
  DynArray*    nodeNames; // String[]
} BuildContext;

AssetAiSource build_source(const BuildContext* ctx, const AssetAiSourceDef* def) {
  AssetAiSource res = {.type = def->type};
  switch (def->type) {
  case AssetAiSource_None:
    break;
  case AssetAiSource_Number:
    res.data_number.value = def->data_number.value;
    break;
  case AssetAiSource_Bool:
    res.data_bool.value = def->data_bool.value;
    break;
  case AssetAiSource_Vector:
    res.data_vector.x = def->data_vector.x;
    res.data_vector.y = def->data_vector.y;
    res.data_vector.z = def->data_vector.z;
    break;
  case AssetAiSource_Time:
    res.data_time.secondsFromNow = def->data_time.secondsFromNow;
    break;
  case AssetAiSource_Knowledge:
    res.data_knowledge.key = stringtable_add(ctx->stringTable, def->data_knowledge.key);
    break;
  }
  return res;
}

static AssetAiNodeId build_node(BuildContext*, const AssetAiNodeDef*);

static AssetAiNodeId build_node_id_peek(const BuildContext* ctx) {
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

static AssetAiNodeKnowledgeSet
build_node_knowledgeset(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeKnowledgeSet){
      .key   = stringtable_add(ctx->stringTable, def->data_knowledgeset.key),
      .value = build_source(ctx, &def->data_knowledgeset.value),
  };
}

static AssetAiNodeKnowledgeCompare
build_node_knowledgecompare(BuildContext* ctx, const AssetAiNodeDef* def) {
  return (AssetAiNodeKnowledgeCompare){
      .comparison = def->data_knowledgecompare.comparison,
      .key        = stringtable_add(ctx->stringTable, def->data_knowledgecompare.key),
      .value      = build_source(ctx, &def->data_knowledgecompare.value),
  };
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
  case AssetAiNode_KnowledgeSet:
    resNode->data_knowledgeset = build_node_knowledgeset(ctx, def);
    break;
  case AssetAiNode_KnowledgeCompare:
    resNode->data_knowledgecompare = build_node_knowledgecompare(ctx, def);
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

  AssetAiNodeDef rootDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataNodeMeta, mem_var(rootDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  DynArray nodes     = dynarray_create_t(g_alloc_heap, AssetAiNode, 64);
  DynArray nodeNames = dynarray_create_t(g_alloc_heap, String, 64);

  BuildContext ctx = {
      .nodes       = &nodes,
      .nodeNames   = &nodeNames,
      .stringTable = g_stringtable,
  };
  build_node(&ctx, &rootDef);

  ecs_world_add_t(
      world,
      entity,
      AssetBehaviorComp,
      .nodes     = dynarray_copy_as_new(&nodes, g_alloc_heap),
      .nodeNames = dynarray_copy_as_new(&nodeNames, g_alloc_heap),
      .nodeCount = nodes.size);

  dynarray_destroy(&nodes);
  dynarray_destroy(&nodeNames);

  data_destroy(g_dataReg, g_alloc_heap, g_dataNodeMeta, mem_var(rootDef));

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
