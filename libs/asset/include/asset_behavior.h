#pragma once
#include "core_dynstring.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Behavior tree definition.
 */

typedef struct sAssetBehavior AssetBehavior;
typedef const AssetBehavior*  AssetBehaviorPtr;

typedef struct {
  AssetBehaviorPtr values;
  usize            count;
} AssetBehaviorList;

typedef enum {
  AssetBehaviorType_Success,
  AssetBehaviorType_Failure,
  AssetBehaviorType_Invert,
  AssetBehaviorType_Parallel,
  AssetBehaviorType_Selector,
  AssetBehaviorType_Sequence,
  AssetBehaviorType_KnowledgeSet,

  AssetBehaviorType_Count,
} AssetBehaviorType;

typedef enum {
  AssetKnowledgeSource_f64,
  AssetKnowledgeSource_Vector,
  AssetKnowledgeSource_Knowledge,
} AssetKnowledgeSourceType;

typedef struct {
  f64 value;
} AssetKnowledgeSourceF64;

typedef struct {
  f32 x, y, z, w;
} AssetKnowledgeSourceVector;

typedef struct {
  String key;
} AssetKnowledgeSourceKnowledge;

typedef struct {
  AssetKnowledgeSourceType type;
  union {
    AssetKnowledgeSourceF64       data_f64;
    AssetKnowledgeSourceVector    data_vector;
    AssetKnowledgeSourceKnowledge data_knowledge;
  };
} AssetKnowledgeSource;

typedef struct {
  AssetBehaviorPtr child;
} AssetBehaviorInvert;

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

typedef struct sAssetBehavior {
  AssetBehaviorType type;
  union {
    AssetBehaviorInvert       data_invert;
    AssetBehaviorParallel     data_parallel;
    AssetBehaviorSelector     data_selector;
    AssetBehaviorSequence     data_sequence;
    AssetBehaviorKnowledgeSet data_knowledgeset;
  };
} AssetBehavior;

ecs_comp_extern_public(AssetBehaviorComp) { AssetBehavior root; };

/**
 * Write a scheme file for the behavior file format.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */
void asset_behavior_scheme_write(DynString*);
