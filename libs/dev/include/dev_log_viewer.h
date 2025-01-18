#pragma once
#include "dev.h"
#include "log_logger.h"

ecs_comp_extern(DevLogViewerComp);

void        dev_log_tracker_init(EcsWorld*, Logger*);
EcsEntityId dev_log_viewer_create(EcsWorld*, EcsEntityId window, LogMask mask);
void        dev_log_viewer_set_mask(DevLogViewerComp*, LogMask mask);
