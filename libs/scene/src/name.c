#include "asset/manager.h"
#include "core/path.h"
#include "core/stringtable.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "scene/marker.h"
#include "scene/name.h"
#include "scene/prefab.h"
#include "scene/renderable.h"
#include "scene/sound.h"
#include "scene/vfx.h"

ecs_comp_define(SceneNameComp);

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
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneLightComp)) {
    return stringtable_add(g_stringtable, string_lit("Light"));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneCollisionComp)) {
    return stringtable_add(g_stringtable, string_lit("Collision"));
  }
  if (ecs_world_has_t(world, ecs_view_entity(entityItr), SceneMarkerComp)) {
    return stringtable_add(g_stringtable, string_lit("Marker"));
  }
  return stringtable_add(g_stringtable, string_lit("unnamed"));
}

ecs_system_define(SceneNameInitSys) {
  EcsIterator* assetItr = ecs_view_itr(ecs_world_view_t(world, AssetView));

  /**
   * Assign a debug name to all level entities without a name.
   */
  EcsView* initView = ecs_world_view_t(world, InitDebugView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const StringHash debugName = scene_name_find(world, itr, assetItr);
    ecs_world_add_t(world, ecs_view_entity(itr), SceneNameComp, .nameDebug = debugName);
  }
}

ecs_module_init(scene_name_module) {
  ecs_register_comp(SceneNameComp);

  ecs_register_view(InitDebugView);
  ecs_register_view(AssetView);

  ecs_register_system(SceneNameInitSys, ecs_view_id(InitDebugView), ecs_view_id(AssetView));
}
