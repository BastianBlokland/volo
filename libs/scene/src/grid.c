#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_grid.h"
#include "scene_renderable.h"

ecs_comp_define(SceneGridComp);

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GridView) { ecs_access_with(SceneGridComp); }

ecs_system_define(SceneGridCreateSys) {
  if (ecs_utils_any(world, GridView)) {
    return;
  }

  EcsView*     view      = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  const EcsEntityId gridEntity = ecs_world_entity_create(world);
  ecs_world_add_empty_t(world, gridEntity, SceneGridComp);
  ecs_world_add_t(
      world,
      gridEntity,
      SceneRenderableUniqueComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/grid.gra")));
}

ecs_module_init(scene_grid_module) {
  ecs_register_comp_empty(SceneGridComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GridView);

  ecs_register_system(SceneGridCreateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GridView));
}
