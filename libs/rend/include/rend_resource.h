#pragma once
#include "ecs_module.h"
#include "geo.h"

// Forward declare from 'asset_manager.h'.
typedef enum eAssetGraphicPass AssetGraphicPass;

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
bool   rend_res_is_loading(const RendResComp*);
bool   rend_res_is_failed(const RendResComp*);
bool   rend_res_is_unused(const RendResComp*);
bool   rend_res_is_persistent(const RendResComp*);
u32    rend_res_ticks_until_unload(const RendResComp*);
u32    rend_res_dependents(const RendResComp*);
u32    rend_res_mesh_vertices(const RendResMeshComp*);
u32    rend_res_mesh_indices(const RendResMeshComp*);
usize  rend_res_mesh_memory(const RendResMeshComp*);
GeoBox rend_res_mesh_bounds(const RendResMeshComp*);
u16    rend_res_texture_width(const RendResTextureComp*);
u16    rend_res_texture_height(const RendResTextureComp*);
u16    rend_res_texture_layers(const RendResTextureComp*);
u8     rend_res_texture_mip_levels(const RendResTextureComp*);
bool   rend_res_texture_is_cube(const RendResTextureComp*);
String rend_res_texture_format_str(const RendResTextureComp*);
usize  rend_res_texture_memory(const RendResTextureComp*);

/**
 * Get the render pass for the given graphic.
 */
AssetGraphicPass rend_res_pass(const RendResGraphicComp*);
i32              rend_res_pass_order(const RendResGraphicComp*);
