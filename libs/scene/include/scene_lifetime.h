#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * The entity is destroyed as soon as its owner is destroyed.
 */
ecs_comp_extern_public(SceneLifetimeOwnerComp) { EcsEntityId owner; };
