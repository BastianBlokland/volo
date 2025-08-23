#pragma once
#include "ecs/module.h"

/**
 * Track the entity that created this entity.
 */
ecs_comp_extern_public(SceneCreatorComp) { EcsEntityId creator; };
