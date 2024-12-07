#pragma once
#include "core_time.h"
#include "ecs_module.h"

#define scene_lifetime_owners_max 2

/**
 * The entity is destroyed as soon as any of the owners are destroyed.
 */
ecs_comp_extern_public(SceneLifetimeOwnerComp) { EcsEntityId owners[scene_lifetime_owners_max]; };

/**
 * The entity is destroyed as when its duration expires.
 */
ecs_comp_extern_public(SceneLifetimeDurationComp) { TimeDuration duration; };
