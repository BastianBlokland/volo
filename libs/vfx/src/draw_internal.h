#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  VfxDrawType_Decal,
  VfxDrawType_DecalDebug,
  VfxDrawType_ParticleForward,
  VfxDrawType_ParticleDistortion,

  VfxDrawType_Count,
} VfxDrawType;

ecs_comp_extern(VfxDrawManagerComp);

EcsEntityId vfx_draw_entity(const VfxDrawManagerComp*, VfxDrawType);
