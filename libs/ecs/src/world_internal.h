#pragma once
#include "ecs_world.h"

typedef struct sEcsView EcsView;

const EcsView* ecs_world_view_storage_internal(const EcsWorld*);

void ecs_world_busy_set(EcsWorld*);
void ecs_world_busy_unset(EcsWorld*);

void ecs_world_stats_update_sys(EcsWorld*, EcsSystemId, JobWorkerId, TimeDuration dur);

void ecs_world_flush_internal(EcsWorld*);
