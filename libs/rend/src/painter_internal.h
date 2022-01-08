#pragma once
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;

ecs_comp_extern_public(RendPainterComp) { RvkCanvas* canvas; };

void rend_painter_teardown(EcsWorld*);
