#pragma once
#include "ecs_world.h"

void ecs_world_busy_set(EcsWorld*);
void ecs_world_busy_unset(EcsWorld*);

void ecs_world_flush(EcsWorld*);
