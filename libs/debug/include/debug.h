#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the debug library.
 */

ecs_comp_extern(DebugFinderComp);
ecs_comp_extern(DebugGizmoComp);
ecs_comp_extern(DebugGridComp);
ecs_comp_extern(DebugLogViewerComp);
ecs_comp_extern(DebugMenuComp);
ecs_comp_extern(DebugPanelComp);
ecs_comp_extern(DebugShapeComp);
ecs_comp_extern(DebugStatsComp);
ecs_comp_extern(DebugStatsGlobalComp);
ecs_comp_extern(DebugTextComp);

typedef enum eDebugPanelType DebugPanelType;
typedef enum eDebugShapeMode DebugShapeMode;
typedef u64                  DebugGizmoId;
