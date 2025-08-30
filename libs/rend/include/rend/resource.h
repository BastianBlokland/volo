#pragma once
#include "asset/forward.h"
#include "ecs/module.h"
#include "geo/forward.h"
#include "rend/forward.h"

/**
 * Renderer resource.
 */
ecs_comp_extern(RendResComp);
ecs_comp_extern(RendResFinishedComp);
ecs_comp_extern(RendResGraphicComp);
ecs_comp_extern(RendResShaderComp);
ecs_comp_extern(RendResMeshComp);
ecs_comp_extern(RendResTextureComp);
ecs_comp_extern(RendResDebugComp); // Debug enabled for this resource.

/**
 * Check the status of the given resource.
 */
bool              rend_res_is_loading(const RendResComp*);
bool              rend_res_is_failed(const RendResComp*);
bool              rend_res_is_unused(const RendResComp*);
bool              rend_res_is_persistent(const RendResComp*);
const RendReport* rend_res_report(const RendResComp*); // Optional.
u32               rend_res_ticks_until_unload(const RendResComp*);
u32               rend_res_dependents(const RendResComp*);
u32               rend_res_mesh_vertices(const RendResMeshComp*);
u32               rend_res_mesh_indices(const RendResMeshComp*);
usize             rend_res_mesh_memory(const RendResMeshComp*);
GeoBox            rend_res_mesh_bounds(const RendResMeshComp*);
u16               rend_res_texture_width(const RendResTextureComp*);
u16               rend_res_texture_height(const RendResTextureComp*);
u16               rend_res_texture_layers(const RendResTextureComp*);
u8                rend_res_texture_mip_levels(const RendResTextureComp*);
bool              rend_res_texture_is_array(const RendResTextureComp*);
bool              rend_res_texture_is_cube(const RendResTextureComp*);
String            rend_res_texture_format_str(const RendResTextureComp*);
usize             rend_res_texture_memory(const RendResTextureComp*);

/**
 * Get the render pass for the given graphic.
 */
AssetGraphicPass rend_res_pass(const RendResGraphicComp*);
i32              rend_res_pass_order(const RendResGraphicComp*);

/**
 * Enable additional debug validation / statistics.
 */
bool rend_res_debug_get(EcsWorld*, EcsEntityId resource);
void rend_res_debug_set(EcsWorld*, EcsEntityId resource, bool value);
