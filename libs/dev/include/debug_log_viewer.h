#pragma once
#include "debug.h"
#include "log_logger.h"

ecs_comp_extern(DebugLogViewerComp);

void        debug_log_tracker_init(EcsWorld*, Logger*);
EcsEntityId debug_log_viewer_create(EcsWorld*, EcsEntityId window, LogMask mask);
void        debug_log_viewer_set_mask(DebugLogViewerComp*, LogMask mask);
