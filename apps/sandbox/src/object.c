#include "asset_manager.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_brain.h"
#include "scene_collision.h"
#include "scene_health.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_renderable.h"
#include "scene_target.h"
#include "scene_transform.h"

#include "object_internal.h"

ecs_comp_define(ObjectDatabaseComp) {
  EcsEntityId unitAGraphic, unitBGraphic;
  EcsEntityId muzzleFlashVfx, projectileVfx, impactVfx;
  EcsEntityId unitBehavior;
  EcsEntityId wallGraphic;
};
ecs_comp_define(ObjectComp);

static SceneLayer object_unit_layer(const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return SceneLayer_UnitFactionA;
  case SceneFaction_B:
    return SceneLayer_UnitFactionB;
  case SceneFaction_C:
    return SceneLayer_UnitFactionC;
  case SceneFaction_D:
    return SceneLayer_UnitFactionD;
  case SceneFaction_Count:
  case SceneFaction_None:
    diag_crash_msg("Unsupported faction");
  }
}

ecs_view_define(GlobalInitView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_without(ObjectDatabaseComp);
}

ecs_system_define(ObjectDatabaseInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalInitView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Already initialized or dependencies not ready.
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      ObjectDatabaseComp,
      .unitAGraphic   = asset_lookup(world, assets, string_lit("graphics/sandbox/swat_a.gra")),
      .unitBGraphic   = asset_lookup(world, assets, string_lit("graphics/sandbox/swat_b.gra")),
      .muzzleFlashVfx = asset_lookup(world, assets, string_lit("vfx/sandbox/muzzleflash.vfx")),
      .projectileVfx  = asset_lookup(world, assets, string_lit("vfx/sandbox/projectile.vfx")),
      .impactVfx      = asset_lookup(world, assets, string_lit("vfx/sandbox/impact.vfx")),
      .unitBehavior   = asset_lookup(world, assets, string_lit("behaviors/unit.bt")),
      .wallGraphic    = asset_lookup(world, assets, string_lit("graphics/sandbox/wall.gra")));
}

ecs_module_init(sandbox_object_module) {
  ecs_register_comp(ObjectDatabaseComp);
  ecs_register_comp_empty(ObjectComp);

  ecs_register_view(GlobalInitView);

  ecs_register_system(ObjectDatabaseInitSys, ecs_view_id(GlobalInitView));
}

EcsEntityId object_spawn_unit(
    EcsWorld*                 world,
    const ObjectDatabaseComp* db,
    const GeoVector           pos,
    const SceneFaction        faction) {
  static const f32                   g_speed   = 4.0f;
  static const SceneCollisionCapsule g_capsule = {
      .offset = {0, 0.3f, 0},
      .radius = 0.3f,
      .height = 1.2f,
  };

  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoQuat     rotation = geo_quat_look(geo_backward, geo_up);

  const EcsEntityId graphic = faction == SceneFaction_A ? db->unitAGraphic : db->unitBGraphic;
  const SceneLayer  layer   = object_unit_layer(faction);

  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = graphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rotation);
  scene_nav_add_agent(world, e);
  ecs_world_add_t(world, e, SceneLocomotionComp, .maxSpeed = g_speed, .radius = 0.4f);
  ecs_world_add_t(world, e, SceneHealthComp, .norm = 1.0f, .max = 100.0f);
  ecs_world_add_t(world, e, SceneFactionComp, .id = faction);
  ecs_world_add_t(world, e, SceneTargetFinderComp);
  ecs_world_add_t(
      world,
      e,
      SceneAttackComp,
      .minInterval    = time_milliseconds(150),
      .maxInterval    = time_milliseconds(300),
      .muzzleFlashVfx = db->muzzleFlashVfx,
      .projectileVfx  = db->projectileVfx,
      .impactVfx      = db->impactVfx);
  scene_collision_add_capsule(world, e, g_capsule, layer);
  scene_brain_add(world, e, db->unitBehavior);
  return e;
}

EcsEntityId object_spawn_wall(
    EcsWorld* world, const ObjectDatabaseComp* db, const GeoVector pos, const GeoQuat rot) {
  static const SceneCollisionBox g_box = {
      .min = {-1, 0, -2},
      .max = {1, 2, 2},
  };

  const EcsEntityId e = ecs_world_entity_create(world);

  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = db->wallGraphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(world, e, SceneScaleComp, .scale = 1.0f);
  scene_collision_add_box(world, e, g_box, SceneLayer_Environment);
  scene_nav_add_blocker(world, e);
  return e;
}
