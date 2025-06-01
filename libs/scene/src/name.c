#include "asset_manager.h"
#include "core_path.h"
#include "core_stringtable.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_light.h"
#include "scene_name.h"
#include "scene_prefab.h"
#include "scene_renderable.h"
#include "scene_sound.h"
#include "scene_vfx.h"

ecs_comp_define_public(SceneNameComp);

ecs_view_define(InitDebugView) {
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_without(SceneNameComp);
  ecs_access_maybe_read(ScenePrefabInstanceComp);
  ecs_access_maybe_read(SceneRenderableComp);
  ecs_access_maybe_read(SceneSoundComp);
  ecs_access_maybe_read(SceneVfxDecalComp);
  ecs_access_maybe_read(SceneVfxSystemComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static StringHash scene_debug_name_from_asset(const AssetComp* assetComp) {
  const String nameStr = path_stem(asset_id(assetComp));
  return stringtable_add(g_stringtable, nameStr);
}

static StringHash scene_name_find(EcsWorld* world, EcsIterator* entityItr, EcsIterator* assetItr) {
  const ScenePrefabInstanceComp* prefabInst = ecs_view_read_t(entityItr, ScenePrefabInstanceComp);
  if (prefabInst) {
    return prefabInst->prefabId;
  }
  const SceneRenderableComp* renderable = ecs_view_read_t(entityItr, SceneRenderableComp);
  if (renderable && ecs_view_maybe_jump(assetItr, renderable->graphic)) {
    return scene_debug_name_from_asset(ecs_view_read_t(assetItr, AssetComp));
  }
  const SceneVfxDecalComp* vfxDecal = ecs_view_read_t(entityItr, SceneVfxDecalComp);
  if (vfxDecal && ecs_view_maybe_jump(assetItr, vfxDecal->asset)) {
    return scene_debug_name_from_asset(ecs_view_read_t(assetItr, AssetComp));
  }
  const SceneVfxSystemComp* vfxSystem = ecs_view_read_t(entityItr, SceneVfxSystemComp);
  if (vfxSystem && ecs_view_maybe_jump(assetItr, vfxSystem->asset)) {
    return scene_debug_name_from_asset(ecs_view_read_t(assetItr, AssetComp));
  }
  const SceneSoundComp* soundComp = ecs_view_read_t(entityItr, SceneSoundComp);
  if (soundComp && ecs_view_maybe_jump(assetItr, soundComp->asset)) {
    return scene_debug_name_from_asset(ecs_view_read_t(assetItr, AssetComp));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneLightPointComp)) {
    return stringtable_add(g_stringtable, string_lit("LightPoint"));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneLightSpotComp)) {
    return stringtable_add(g_stringtable, string_lit("LightSpot"));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneLightLineComp)) {
    return stringtable_add(g_stringtable, string_lit("LightLine"));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneLightDirComp)) {
    return stringtable_add(g_stringtable, string_lit("LightDir"));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneCollisionComp)) {
    return stringtable_add(g_stringtable, string_lit("Collision"));
  }
  return stringtable_add(g_stringtable, string_lit("unnamed"));
}

ecs_system_define(SceneNameInitSys) {
  EcsIterator* assetItr = ecs_view_itr(ecs_world_view_t(world, AssetView));

  /**
   * For level entity that don't have a name automatically assign one for debug purposes.
   */
  EcsView* initView = ecs_world_view_t(world, InitDebugView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const StringHash debugName = scene_name_find(world, itr, assetItr);
    ecs_world_add_t(world, ecs_view_entity(itr), SceneNameComp, .name = debugName);
  }
}

ecs_module_init(scene_name_module) {
  ecs_register_comp(SceneNameComp);

  ecs_register_view(InitDebugView);
  ecs_register_view(AssetView);

  ecs_register_system(SceneNameInitSys, ecs_view_id(InitDebugView), ecs_view_id(AssetView));
}
