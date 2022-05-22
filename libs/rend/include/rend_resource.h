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
 * Check the status of the given resource.
 */
bool rend_res_is_loading(const RendResComp*);
bool rend_res_is_failed(const RendResComp*);
bool rend_res_is_unused(const RendResComp*);
u64  rend_res_ticks_until_unload(const RendResComp*);

/**
 * Get the render-order for the given graphic.
 */
i32 rend_res_render_order(const RendResGraphicComp*);
