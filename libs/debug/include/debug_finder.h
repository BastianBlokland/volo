#pragma once
#include "ecs_module.h"

typedef enum {
  DebugFinderCategory_Level,
  DebugFinderCategory_Terrain,

  DebugFinderCategory_Count,
} DebugFinderCategory;

typedef enum {
  DebugFinderState_Pending,
  DebugFinderState_Ready,
} DebugFinderState;

typedef struct {
  DebugFinderState   state;
  u32                count;
  const EcsEntityId* entities;
  const String*      ids;
} DebugFinderResult;

ecs_comp_extern(DebugAssetFinderComp);

void              debug_asset_query(DebugAssetFinderComp*, DebugFinderCategory, bool refresh);
DebugFinderResult debug_finder_get(DebugAssetFinderComp*, DebugFinderCategory);
