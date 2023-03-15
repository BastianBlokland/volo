#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  VfxDrawType_Decal,

  VfxDrawType_Count,
} VfxDrawType;

ecs_comp_extern(VfxDrawManagerComp);

/**
 * Tag components for different draw types.
 * Can be used to constrain different systems to allow parallel draw creation.
 */
ecs_comp_extern(VfxDrawDecalComp);

EcsEntityId vfx_draw_entity(const VfxDrawManagerComp*, VfxDrawType);
