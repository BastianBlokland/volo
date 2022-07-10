#include "asset_manager.h"
#include "core_path.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_name.h"
#include "scene_renderable.h"

ecs_comp_define_public(SceneNameComp);

ecs_view_define(NameInitView) {
  ecs_access_without(SceneNameComp);
  ecs_access_read(SceneRenderableComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

ecs_system_define(SceneNameInitSys) {
  EcsIterator* assetItr = ecs_view_itr(ecs_world_view_t(world, AssetView));

  /**
   * Assign names based on the identifier of their graphic.
   */
  EcsView* initView = ecs_world_view_t(world, NameInitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    if (ecs_view_maybe_jump(assetItr, renderable->graphic)) {
      const AssetComp* assetComp = ecs_view_read_t(assetItr, AssetComp);
      const StringHash name      = stringtable_add(g_stringtable, path_stem(asset_id(assetComp)));
      ecs_world_add_t(world, ecs_view_entity(itr), SceneNameComp, .name = name);
    }
  }
}

ecs_module_init(scene_name_module) {
  ecs_register_comp(SceneNameComp);

  ecs_register_view(NameInitView);
  ecs_register_view(AssetView);

  ecs_register_system(SceneNameInitSys, ecs_view_id(NameInitView), ecs_view_id(AssetView));
}
