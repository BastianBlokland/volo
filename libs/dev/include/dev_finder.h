#pragma once
#include "dev.h"

typedef enum eDevFinderCategory {
  DevFinder_Decal,
  DevFinder_Graphic,
  DevFinder_Level,
  DevFinder_Sound,
  DevFinder_Terrain,
  DevFinder_Vfx,

  DevFinderCategory_Count,
} DevFinderCategory;

extern const String g_debugFinderCategoryNames[DevFinderCategory_Count];

typedef enum {
  DevFinderStatus_Idle,
  DevFinderStatus_Loading,
  DevFinderStatus_Ready,
} DevFinderStatus;

typedef struct {
  DevFinderStatus    status;
  u32                count;
  const EcsEntityId* entities;
  const String*      ids;
} DevFinderResult;

ecs_comp_extern(DevFinderComp);

void            dev_finder_query(DevFinderComp*, DevFinderCategory, bool refresh);
DevFinderResult dev_finder_get(DevFinderComp*, DevFinderCategory);
