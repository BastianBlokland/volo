#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "log_logger.h"

ecs_comp_extern(DebugLogViewerComp);

EcsEntityId debug_log_viewer_create(EcsWorld*, EcsEntityId window, LogMask mask);
void        debug_log_viewer_set_mask(DebugLogViewerComp*, LogMask mask);
