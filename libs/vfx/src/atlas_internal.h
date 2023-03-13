#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  VfxAtlasType_Particle,
  VfxAtlasType_DecalColor,

  VfxAtlasType_Count,
} VfxAtlasType;

ecs_comp_extern(VfxAtlasManagerComp);

EcsEntityId vfx_atlas_entity(const VfxAtlasManagerComp*, VfxAtlasType);
