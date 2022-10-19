#pragma once
#include "ecs_world.h"

typedef struct sEcsView EcsView;

const EcsView* ecs_world_view_storage_internal(const EcsWorld*);

void ecs_world_busy_set(EcsWorld*);
void ecs_world_busy_unset(EcsWorld*);

/**
 * Add to the system stats for the given system in this frame.
 * NOTE: Can be called in parallel for the same system.
 */
void ecs_world_stats_sys_add(EcsWorld*, EcsSystemId, TimeDuration dur);

void ecs_world_flush_internal(EcsWorld*);
