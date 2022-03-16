#pragma once
#include "core_dynarray.h"
#include "ecs_entity.h"
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;

ecs_comp_extern_public(RendPainterComp) {
  DynArray   drawBuffer; // RvkPassDraw[]
  RvkCanvas* canvas;
};

void rend_painter_teardown(EcsWorld* world, EcsEntityId entity);
