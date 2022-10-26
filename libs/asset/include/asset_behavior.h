#pragma once
#include "core_dynstring.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Behavior tree definition.
 */

typedef struct sAssetAiNode AssetAiNode;

typedef struct {
  const AssetAiNode* values;
  usize              count;
} AssetAiNodeList;

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
  AssetAiSource_None,
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
  String key;
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
  const AssetAiNode* child;
} AssetAiNodeInvert;

typedef struct {
  const AssetAiNode* child;
} AssetAiNodeTry;

typedef struct {
  const AssetAiNode* child;
} AssetAiNodeRepeat;

typedef struct {
  AssetAiNodeList children;
} AssetAiNodeParallel;

typedef struct {
  AssetAiNodeList children;
} AssetAiNodeSelector;

typedef struct {
  AssetAiNodeList children;
} AssetAiNodeSequence;

typedef struct {
  String        key;
  AssetAiSource value;
} AssetAiNodeKnowledgeSet;

typedef struct {
  AssetAiComparison comparison;
  String            key;
  AssetAiSource     value;
} AssetAiNodeKnowledgeCompare;

typedef struct sAssetAiNode {
  AssetAiNodeType type;
  String          name;
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

ecs_comp_extern_public(AssetBehaviorComp) { AssetAiNode root; };

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
