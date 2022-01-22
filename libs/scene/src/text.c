#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_text.h"

static const String g_textGraphic = string_static("graphics/ui/text.gra");

ecs_comp_define_public(SceneTextComp);

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(TextInitView) {
  ecs_access_with(SceneTextComp);
  ecs_access_without(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  EcsView* renderView = ecs_world_view_t(world, TextInitView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    ecs_world_add_t(
        world,
        ecs_view_entity(itr),
        SceneRenderableUniqueComp,
        .graphic = asset_lookup(world, assets, g_textGraphic));
  }
}

ecs_view_define(TextRenderView) {
  ecs_access_read(SceneTextComp);
  ecs_access_write(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextRenderSys) {
  EcsView* renderView = ecs_world_view_t(world, TextRenderView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    const SceneTextComp*       textComp   = ecs_view_read_t(itr, SceneTextComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);

    (void)textComp;
    renderable->vertexCountOverride = 6;
  }
}

ecs_module_init(scene_text_module) {
  ecs_register_comp(SceneTextComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(TextInitView);
  ecs_register_view(TextRenderView);

  ecs_register_system(SceneTextInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(TextInitView));
  ecs_register_system(SceneTextRenderSys, ecs_view_id(TextRenderView));
}
