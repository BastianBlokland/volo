#pragma once
#include "ecs_module.h"

/**
 * Renderer resource.
 */
ecs_comp_extern(RendResComp);
ecs_comp_extern(RendResGraphicComp);
ecs_comp_extern(RendResShaderComp);
ecs_comp_extern(RendResMeshComp);
ecs_comp_extern(RendResTextureComp);

/**
 * Get the render-order for the given graphic.
 */
i32 rend_res_render_order(const RendResGraphicComp*);
