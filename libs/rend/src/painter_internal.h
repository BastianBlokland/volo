#pragma once
#include "core_dynarray.h"
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;

ecs_comp_extern_public(RendPainterComp) {
  DynArray   drawBuffer; // RvkPassDraw[]
  RvkCanvas* canvas;
};
