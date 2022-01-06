#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "loader_shader_internal.h"

ecs_comp_define_public(AssetShaderComp);
ecs_comp_define_public(AssetShaderSourceComp);

static void ecs_destruct_shader_comp(void* data) {
  AssetShaderComp* comp = data;
  if (comp->resources.values) {
    alloc_free_array_t(g_alloc_heap, comp->resources.values, asset_shader_max_resources);
  }
  if (comp->specs.values) {
    alloc_free_array_t(g_alloc_heap, comp->specs.values, asset_shader_max_specs);
  }
}

static void ecs_destruct_shader_source_comp(void* data) {
  AssetShaderSourceComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetShaderComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any shader-asset components for unloaded assets.
 */
ecs_system_define(UnloadShaderAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetShaderComp);
    ecs_utils_maybe_remove_t(world, entity, AssetShaderSourceComp);
  }
}

ecs_module_init(asset_shader_module) {
  ecs_register_comp(AssetShaderComp, .destructor = ecs_destruct_shader_comp);
  ecs_register_comp(AssetShaderSourceComp, .destructor = ecs_destruct_shader_source_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadShaderAssetSys, ecs_view_id(UnloadView));
}
