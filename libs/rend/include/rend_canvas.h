#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Ecs component for a render canvas.
 */
ecs_comp_extern(RendCanvasComp);

/**
 * Create a new render canvas on a window.
 */
void rend_canvas_create(EcsWorld*, EcsEntityId windowEntity);
