#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * The entity is destroyed as soon as its owner is destroyed.
 */
ecs_comp_extern_public(SceneLifetimeOwnerComp) { EcsEntityId owner; };

/**
 * The entity is destroyed as when its duration expires.
 */
ecs_comp_extern_public(SceneLifetimeDurationComp) { TimeDuration duration; };
