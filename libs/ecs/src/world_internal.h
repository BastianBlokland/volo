#pragma once
#include "ecs_world.h"

void ecs_world_busy_set(EcsWorld*);
void ecs_world_busy_unset(EcsWorld*);

void ecs_world_stats_update_sys(EcsWorld*, EcsSystemId, JobWorkerId, TimeDuration dur);

void ecs_world_flush_internal(EcsWorld*);
