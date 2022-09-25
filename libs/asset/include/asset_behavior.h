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

typedef struct {
  const String* values;
  usize         count;
} AssetKnowledgeList;

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
  AssetBehavior_KnowledgeClear,
  AssetBehavior_KnowledgeCheck,
  AssetBehavior_KnowledgeCompare,

  AssetBehavior_Count,
} AssetBehaviorType;

typedef enum {
  AssetKnowledgeComparison_Equal,
  AssetKnowledgeComparison_Less,
} AssetKnowledgeComparison;

typedef enum {
  AssetKnowledgeSource_Number,
  AssetKnowledgeSource_Bool,
  AssetKnowledgeSource_Vector,
  AssetKnowledgeSource_Knowledge,
} AssetKnowledgeSourceType;

typedef struct {
  f64 value;
} AssetKnowledgeSourceNumber;

typedef struct {
  bool value;
} AssetKnowledgeSourceBool;

typedef struct {
  f32 x, y, z, w;
} AssetKnowledgeSourceVector;

typedef struct {
  String key;
} AssetKnowledgeSourceKnowledge;

typedef struct {
  AssetKnowledgeSourceType type;
  union {
    AssetKnowledgeSourceNumber    data_number;
    AssetKnowledgeSourceBool      data_bool;
    AssetKnowledgeSourceVector    data_vector;
    AssetKnowledgeSourceKnowledge data_knowledge;
  };
} AssetKnowledgeSource;

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
  String               key;
  AssetKnowledgeSource value;
} AssetBehaviorKnowledgeSet;

typedef struct {
  AssetKnowledgeList keys;
} AssetBehaviorKnowledgeClear;

typedef struct {
  AssetKnowledgeList keys;
} AssetBehaviorKnowledgeCheck;

typedef struct {
  AssetKnowledgeComparison comparison;
  String                   key;
  AssetKnowledgeSource     value;
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
    AssetBehaviorKnowledgeClear   data_knowledgeclear;
    AssetBehaviorKnowledgeCheck   data_knowledgecheck;
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
