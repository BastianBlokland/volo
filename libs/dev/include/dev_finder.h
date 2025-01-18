#pragma once
#include "dev.h"

typedef enum eDebugFinderCategory {
  DebugFinder_Decal,
  DebugFinder_Graphic,
  DebugFinder_Level,
  DebugFinder_Sound,
  DebugFinder_Terrain,
  DebugFinder_Vfx,

  DebugFinderCategory_Count,
} DebugFinderCategory;

extern const String g_debugFinderCategoryNames[DebugFinderCategory_Count];

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

ecs_comp_extern(DevFinderComp);

void              dev_finder_query(DevFinderComp*, DebugFinderCategory, bool refresh);
DebugFinderResult dev_finder_get(DevFinderComp*, DebugFinderCategory);
