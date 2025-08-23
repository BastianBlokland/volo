#pragma once
#include "ecs/module.h"
#include "rvk/forward.h"

#include "forward.h"

ecs_comp_extern_public(RendPainterComp) { RvkCanvas* canvas; };

void rend_painter_teardown(EcsWorld* world, EcsEntityId entity);
