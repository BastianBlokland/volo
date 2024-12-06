#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#include "forward_internal.h"
#include "rvk/forward_internal.h"

ecs_comp_extern_public(RendPainterComp) { RvkCanvas* canvas; };

void rend_painter_teardown(EcsWorld* world, EcsEntityId entity);
