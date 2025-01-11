#pragma once
#include "debug.h"

typedef enum eDebugFinderCategory {
  DebugFinderCategory_Level,
  DebugFinderCategory_Terrain,

  DebugFinderCategory_Count,
} DebugFinderCategory;

typedef enum {
  DebugFinderStatus_Idle,
  DebugFinderStatus_Loading,
  DebugFinderStatus_Ready,
} DebugFinderStatus;

typedef struct {
  DebugFinderStatus  status;
  u32                count;
  const EcsEntityId* entities;
  const String*      ids;
} DebugFinderResult;

ecs_comp_extern(DebugFinderComp);

void              debug_finder_query(DebugFinderComp*, DebugFinderCategory, bool refresh);
DebugFinderResult debug_finder_get(DebugFinderComp*, DebugFinderCategory);
