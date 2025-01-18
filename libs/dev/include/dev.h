#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the development library.
 */

ecs_comp_extern(DebugFinderComp);
ecs_comp_extern(DebugGizmoComp);
ecs_comp_extern(DebugGridComp);
ecs_comp_extern(DebugLogViewerComp);
ecs_comp_extern(DevMenuComp);
ecs_comp_extern(DevPanelComp);
ecs_comp_extern(DebugShapeComp);
ecs_comp_extern(DebugStatsComp);
ecs_comp_extern(DebugStatsGlobalComp);
ecs_comp_extern(DebugTextComp);

typedef enum eDebugFinderCategory DebugFinderCategory;
typedef enum eDevPanelType        DevPanelType;
typedef enum eDebugShapeMode      DebugShapeMode;
typedef u64                       DebugGizmoId;
