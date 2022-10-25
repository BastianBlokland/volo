#pragma once
#include "core_dynstring.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Behavior tree definition.
 */

typedef struct sAssetBehavior AssetBehavior;

typedef struct {
  const AssetBehavior* values;
  usize                count;
} AssetBehaviorList;

typedef enum eAssetBehaviorType {
  AssetBehavior_Running,
  AssetBehavior_Success,
  AssetBehavior_Failure,
  AssetBehavior_Invert,
  AssetBehavior_Try,
  AssetBehavior_Repeat,
  AssetBehavior_Parallel,
  AssetBehavior_Selector,
  AssetBehavior_Sequence,
  AssetBehavior_KnowledgeSet,
  AssetBehavior_KnowledgeCompare,

  AssetBehavior_Count,
} AssetBehaviorType;

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
  const AssetBehavior* child;
} AssetBehaviorInvert;

typedef struct {
  const AssetBehavior* child;
} AssetBehaviorTry;

typedef struct {
  const AssetBehavior* child;
} AssetBehaviorRepeat;

typedef struct {
  AssetBehaviorList children;
} AssetBehaviorParallel;

typedef struct {
  AssetBehaviorList children;
} AssetBehaviorSelector;

typedef struct {
  AssetBehaviorList children;
} AssetBehaviorSequence;

typedef struct {
  String        key;
  AssetAiSource value;
} AssetBehaviorKnowledgeSet;

typedef struct {
  AssetAiComparison comparison;
  String            key;
  AssetAiSource     value;
} AssetBehaviorKnowledgeCompare;

typedef struct sAssetBehavior {
  AssetBehaviorType type;
  String            name;
  union {
    AssetBehaviorInvert           data_invert;
    AssetBehaviorTry              data_try;
    AssetBehaviorRepeat           data_repeat;
    AssetBehaviorParallel         data_parallel;
    AssetBehaviorSelector         data_selector;
    AssetBehaviorSequence         data_sequence;
    AssetBehaviorKnowledgeSet     data_knowledgeset;
    AssetBehaviorKnowledgeCompare data_knowledgecompare;
  };
} AssetBehavior;

ecs_comp_extern_public(AssetBehaviorComp) { AssetBehavior root; };

/**
 * Get a textual representation of the given type enumeration.
 * Pre-condition: type >= 0 && type < AssetBehavior_Count.
 */
String asset_behavior_type_str(AssetBehaviorType);

/**
 * Write a scheme file for the behavior file format.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */
void asset_behavior_scheme_write(DynString*);
