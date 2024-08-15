#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  VfxDrawType_DecalSingle,
  VfxDrawType_DecalSingleDebug,
  VfxDrawType_DecalTrail,
  VfxDrawType_DecalTrailDebug,
  VfxDrawType_ParticleSpriteForward,
  VfxDrawType_ParticleSpriteDistortion,

  VfxDrawType_Count,
} VfxDrawType;

ecs_comp_extern(VfxDrawManagerComp);

EcsEntityId vfx_draw_entity(const VfxDrawManagerComp*, VfxDrawType);
