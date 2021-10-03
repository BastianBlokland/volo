#pragma once
#include "ecs_entity.h"

/**
 * Ecs View.
 * Handle for accessing component data.
 */
typedef struct sEcsView EcsView;

/**
 * Check if this view contains the given entity.
 */
bool ecs_view_contains(EcsView*, EcsEntityId);
