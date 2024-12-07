#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(SceneVfxSystemComp) {
  EcsEntityId asset; // Vfx system asset.
  f32         alpha, emitMultiplier;
};

ecs_comp_extern_public(SceneVfxDecalComp) {
  EcsEntityId asset; // Decal asset.
  f32         alpha;
};
