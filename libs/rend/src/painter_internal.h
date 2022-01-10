#pragma once
#include "core_dynarray.h"
#include "ecs_entity.h"
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkCanvas  RvkCanvas;
typedef struct sRvkGraphic RvkGraphic;

ecs_comp_extern_public(RendPainterComp) { RvkCanvas* canvas; };

ecs_comp_extern_public(RendPainterDrawComp) {
  EcsEntityId graphic;
  u32         vertexCountOverride;
  DynArray    instances;
};

void rend_painter_teardown(EcsWorld*);
