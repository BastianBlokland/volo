#pragma once
#include "core_dynarray.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(RendDrawComp) {
  EcsEntityId graphic;
  u32         vertexCountOverride;
  DynArray    instances;
};
