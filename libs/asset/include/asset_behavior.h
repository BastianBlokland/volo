#pragma once
#include "core_dynstring.h"
#include "ecs_module.h"

/**
 * Behavior tree definition.
 */

typedef struct sAssetBehavior AssetBehavior;

typedef struct {
  AssetBehavior* values;
  usize          count;
} AssetBehaviorList;

typedef enum {
  AssetBehaviorType_Success,
  AssetBehaviorType_Failure,
  AssetBehaviorType_Invert,
  AssetBehaviorType_Parallel,
  AssetBehaviorType_Selector,
  AssetBehaviorType_Sequence,
} AssetBehaviorType;

typedef struct {
  AssetBehavior* child;
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

typedef struct sAssetBehavior {
  AssetBehaviorType type;
  union {
    AssetBehaviorInvert   data_invert;
    AssetBehaviorParallel data_parallel;
    AssetBehaviorSelector data_selector;
    AssetBehaviorSequence data_sequence;
  };
} AssetBehavior;

ecs_comp_extern_public(AssetBehaviorComp) { AssetBehavior root; };

/**
 * Write a scheme file for the behavior file format.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */
void asset_behavior_scheme_write(DynString*);
