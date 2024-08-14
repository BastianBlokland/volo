#include "asset_manager.h"
#include "core_path.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_level.h"
#include "scene_name.h"
#include "scene_renderable.h"
#include "scene_vfx.h"

ecs_comp_define_public(SceneNameComp);

ecs_view_define(InitDebugView) {
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_without(SceneNameComp);
  ecs_access_maybe_read(SceneRenderableComp);
  ecs_access_maybe_read(SceneVfxDecalComp);
  ecs_access_maybe_read(SceneVfxSystemComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static String scene_debug_name_from_asset(const AssetComp* assetComp) {
  return path_stem(asset_id(assetComp));
}

static String scene_debug_name_find(EcsIterator* entityItr, EcsIterator* assetItr) {
  const SceneRenderableComp* renderable = ecs_view_read_t(entityItr, SceneRenderableComp);
  if (renderable && ecs_view_maybe_jump(assetItr, renderable->graphic)) {
    const AssetComp* assetComp = ecs_view_read_t(assetItr, AssetComp);
    return scene_debug_name_from_asset(assetComp);
  }
  const SceneVfxDecalComp* vfxDecal = ecs_view_read_t(entityItr, SceneVfxDecalComp);
  if (vfxDecal && ecs_view_maybe_jump(assetItr, vfxDecal->asset)) {
    const AssetComp* assetComp = ecs_view_read_t(assetItr, AssetComp);
    return scene_debug_name_from_asset(assetComp);
  }
  const SceneVfxSystemComp* vfxSystem = ecs_view_read_t(entityItr, SceneVfxSystemComp);
  if (vfxSystem && ecs_view_maybe_jump(assetItr, vfxSystem->asset)) {
    const AssetComp* assetComp = ecs_view_read_t(assetItr, AssetComp);
    return scene_debug_name_from_asset(assetComp);
  }
  return string_lit("unnamed");
}

ecs_system_define(SceneNameInitSys) {
  EcsIterator* assetItr = ecs_view_itr(ecs_world_view_t(world, AssetView));

  /**
   * For level entity that don't have a name automatically assign one for debug purposes.
   */
  EcsView* initView = ecs_world_view_t(world, InitDebugView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const String     debugName     = scene_debug_name_find(itr, assetItr);
    const StringHash debugNameHash = stringtable_add(g_stringtable, debugName);
    ecs_world_add_t(world, ecs_view_entity(itr), SceneNameComp, .name = debugNameHash);
  }
}

ecs_module_init(scene_name_module) {
  ecs_register_comp(SceneNameComp);

  ecs_register_view(InitDebugView);
  ecs_register_view(AssetView);

  ecs_register_system(SceneNameInitSys, ecs_view_id(InitDebugView), ecs_view_id(AssetView));
}
