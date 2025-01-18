#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the development library.
 */

ecs_comp_extern(DevFinderComp);
ecs_comp_extern(DevGizmoComp);
ecs_comp_extern(DevGridComp);
ecs_comp_extern(DevLogViewerComp);
ecs_comp_extern(DevMenuComp);
ecs_comp_extern(DevPanelComp);
ecs_comp_extern(DevShapeComp);
ecs_comp_extern(DevStatsComp);
ecs_comp_extern(DevStatsGlobalComp);
ecs_comp_extern(DevTextComp);

typedef enum eDevFinderCategory DevFinderCategory;
typedef enum eDevPanelType      DevPanelType;
typedef enum eDevShapeMode      DevShapeMode;
typedef u64                     DevGizmoId;
