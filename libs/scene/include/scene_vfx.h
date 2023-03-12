#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Vfx system.
 */
ecs_comp_extern_public(SceneVfxComp) {
  EcsEntityId asset; // Vfx system asset.
  f32         alpha;
};

/**
 * Decal.
 */
ecs_comp_extern_public(SceneDecalComp) {
  EcsEntityId asset; // Decal asset.
};
