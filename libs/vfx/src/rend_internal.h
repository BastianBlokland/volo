#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  VfxRendObj_DecalStampSingle,
  VfxRendObj_DecalStampSingleDebug,
  VfxRendObj_DecalStampTrail,
  VfxRendObj_DecalStampTrailDebug,
  VfxRendObj_ParticleSpriteForward,
  VfxRendObj_ParticleSpriteDistortion,

  VfxRendObj_Count,
} VfxRendObj;

ecs_comp_extern(VfxRendComp);

EcsEntityId vfx_rend_obj(const VfxRendComp*, VfxRendObj);
