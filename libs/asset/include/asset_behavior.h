#pragma once
#include "asset_data.h"
#include "ecs_module.h"
#include "geo_vector.h"

// Forward declare from 'script_doc.h'.
typedef struct sScriptDoc ScriptDoc;
typedef u32               ScriptExpr;

/**
 * Behavior tree definition.
 */

typedef u16 AssetAiNodeId;

enum { AssetAiNodeRoot = 0 };

typedef enum eAssetAiNodeType {
  AssetAiNode_Running,
  AssetAiNode_Success,
  AssetAiNode_Failure,
  AssetAiNode_Invert,
  AssetAiNode_Try,
  AssetAiNode_Repeat,
  AssetAiNode_Parallel,
  AssetAiNode_Selector,
  AssetAiNode_Sequence,
  AssetAiNode_Condition,
  AssetAiNode_Execute,

  AssetAiNode_Count,
} AssetAiNodeType;

typedef struct {
  AssetAiNodeId child;
} AssetAiNodeInvert;

typedef struct {
  AssetAiNodeId child;
} AssetAiNodeTry;

typedef struct {
  AssetAiNodeId child;
} AssetAiNodeRepeat;

typedef struct {
  AssetAiNodeId childrenBegin;
} AssetAiNodeParallel;

typedef struct {
  AssetAiNodeId childrenBegin;
} AssetAiNodeSelector;

typedef struct {
  AssetAiNodeId childrenBegin;
} AssetAiNodeSequence;

typedef struct {
  ScriptExpr scriptExpr;
} AssetAiNodeCondition;

typedef struct {
  ScriptExpr scriptExpr;
} AssetAiNodeExecute;

typedef struct sAssetAiNode {
  AssetAiNodeType type;
  AssetAiNodeId   nextSibling;
  union {
    AssetAiNodeInvert    data_invert;
    AssetAiNodeTry       data_try;
    AssetAiNodeRepeat    data_repeat;
    AssetAiNodeParallel  data_parallel;
    AssetAiNodeSelector  data_selector;
    AssetAiNodeSequence  data_sequence;
    AssetAiNodeCondition data_condition;
    AssetAiNodeExecute   data_execute;
  };
} AssetAiNode;

ecs_comp_extern_public(AssetBehaviorComp) {
  const AssetAiNode* nodes;     // AssetAiNode[nodeCount]
  const String*      nodeNames; // String[nodeCount]
  u16                nodeCount;
  const ScriptDoc*   scriptDoc;
};

/**
 * Get a textual representation of the given type enumeration.
 * Pre-condition: type >= 0 && type < AssetAiNode_Count.
 */
String asset_behavior_type_str(AssetAiNodeType);

AssetDataReg asset_behavior_datareg();
