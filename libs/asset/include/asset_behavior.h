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

typedef enum {
  AssetBehavior_Success,
  AssetBehavior_Failure,
  AssetBehavior_Invert,
  AssetBehavior_Parallel,
  AssetBehavior_Selector,
  AssetBehavior_Sequence,
  AssetBehavior_KnowledgeSet,
  AssetBehavior_KnowledgeClear,
  AssetBehavior_KnowledgeCheck,

  AssetBehavior_Count,
} AssetBehaviorType;

typedef enum {
  AssetKnowledgeSource_Number,
  AssetKnowledgeSource_Vector,
  AssetKnowledgeSource_Knowledge,
} AssetKnowledgeSourceType;

typedef struct {
  f64 value;
} AssetKnowledgeSourceNumber;

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
    AssetKnowledgeSourceVector    data_vector;
    AssetKnowledgeSourceKnowledge data_knowledge;
  };
} AssetKnowledgeSource;

typedef struct {
  const AssetBehavior* child;
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

typedef struct {
  AssetKnowledgeList keys;
} AssetBehaviorKnowledgeClear;

typedef struct {
  String key;
} AssetBehaviorKnowledgeCheck;

typedef struct sAssetBehavior {
  AssetBehaviorType type;
  union {
    AssetBehaviorInvert         data_invert;
    AssetBehaviorParallel       data_parallel;
    AssetBehaviorSelector       data_selector;
    AssetBehaviorSequence       data_sequence;
    AssetBehaviorKnowledgeSet   data_knowledgeset;
    AssetBehaviorKnowledgeClear data_knowledgeclear;
    AssetBehaviorKnowledgeCheck data_knowledgecheck;
  };
} AssetBehavior;

ecs_comp_extern_public(AssetBehaviorComp) { AssetBehavior root; };

/**
 * Write a scheme file for the behavior file format.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */
void asset_behavior_scheme_write(DynString*);
