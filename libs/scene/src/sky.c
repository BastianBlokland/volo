#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_tag.h"

ecs_comp_define(SceneSkyComp);

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(SkyView) { ecs_access_with(SceneSkyComp); }

ecs_system_define(SceneCreateSkySys) {
  if (ecs_utils_any(world, SkyView)) {
    return;
  }

  EcsView*     view      = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_empty_t(world, entity, SceneSkyComp);
  ecs_world_add_t(
      world,
      entity,
      SceneRenderableComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/scene/sky.gra")));
  ecs_world_add_t(world, entity, SceneTagComp, .tags = SceneTags_Background);
}

ecs_module_init(scene_sky_module) {
  ecs_register_comp_empty(SceneSkyComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(SkyView);

  ecs_register_system(SceneCreateSkySys, ecs_view_id(GlobalAssetsView), ecs_view_id(SkyView));
}
