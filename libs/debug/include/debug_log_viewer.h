#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "log_logger.h"

EcsEntityId debug_log_viewer_create(EcsWorld*, EcsEntityId window, LogMask mask);
