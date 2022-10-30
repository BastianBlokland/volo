#pragma once
#include "core_dynstring.h"
#include "ecs_module.h"
#include "geo_vector.h"

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
  AssetAiNode_KnowledgeSet,
  AssetAiNode_KnowledgeCompare,

  AssetAiNode_Count,
} AssetAiNodeType;

typedef enum {
  AssetAiComparison_Equal,
  AssetAiComparison_NotEqual,
  AssetAiComparison_Less,
  AssetAiComparison_LessOrEqual,
  AssetAiComparison_Greater,
  AssetAiComparison_GreaterOrEqual,
} AssetAiComparison;

typedef enum {
  AssetAiSource_Null,
  AssetAiSource_Number,
  AssetAiSource_Bool,
  AssetAiSource_Vector,
  AssetAiSource_Time,
  AssetAiSource_Knowledge,
} AssetAiSourceType;

typedef struct {
  f64 value;
} AssetAiSourceNumber;

typedef struct {
  bool value;
} AssetAiSourceBool;

typedef struct {
  f32 x, y, z;
} AssetAiSourceVector;

typedef struct {
  f32 secondsFromNow;
} AssetAiSourceTime;

typedef struct {
  StringHash key;
} AssetAiSourceKnowledge;

typedef struct {
  AssetAiSourceType type;
  union {
    AssetAiSourceNumber    data_number;
    AssetAiSourceBool      data_bool;
    AssetAiSourceVector    data_vector;
    AssetAiSourceTime      data_time;
    AssetAiSourceKnowledge data_knowledge;
  };
} AssetAiSource;

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
  StringHash    key;
  AssetAiSource value;
} AssetAiNodeKnowledgeSet;

typedef struct {
  AssetAiComparison comparison;
  StringHash        key;
  AssetAiSource     value;
} AssetAiNodeKnowledgeCompare;

typedef struct sAssetAiNode {
  AssetAiNodeType type;
  AssetAiNodeId   nextSibling;
  union {
    AssetAiNodeInvert           data_invert;
    AssetAiNodeTry              data_try;
    AssetAiNodeRepeat           data_repeat;
    AssetAiNodeParallel         data_parallel;
    AssetAiNodeSelector         data_selector;
    AssetAiNodeSequence         data_sequence;
    AssetAiNodeKnowledgeSet     data_knowledgeset;
    AssetAiNodeKnowledgeCompare data_knowledgecompare;
  };
} AssetAiNode;

ecs_comp_extern_public(AssetBehaviorComp) {
  const AssetAiNode* nodes;     // AssetAiNode[nodeCount]
  const String*      nodeNames; // String[nodeCount]
  u16                nodeCount;
};

/**
 * Get a textual representation of the given type enumeration.
 * Pre-condition: type >= 0 && type < AssetAiNode_Count.
 */
String asset_behavior_type_str(AssetAiNodeType);

/**
 * Write a scheme file for the behavior file format.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */
void asset_behavior_scheme_write(DynString*);
