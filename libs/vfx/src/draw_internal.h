#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  VfxDrawType_DecalStampSingle,
  VfxDrawType_DecalStampSingleDebug,
  VfxDrawType_DecalStampTrail,
  VfxDrawType_DecalStampTrailDebug,
  VfxDrawType_ParticleSpriteForward,
  VfxDrawType_ParticleSpriteDistortion,

  VfxDrawType_Count,
} VfxDrawType;

ecs_comp_extern(VfxDrawManagerComp);

EcsEntityId vfx_draw_entity(const VfxDrawManagerComp*, VfxDrawType);
