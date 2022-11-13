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
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_transform.h"

#include "object_internal.h"

ecs_comp_define(ObjectDatabaseComp) {
  EcsEntityId unitAGraphic, unitBGraphic, unitCGraphic;
  EcsEntityId vfxMuzzleFlash, vfxProjectile, vfxImpact;
  EcsEntityId unitBehaviorAuto, unitBehaviorManual;
  EcsEntityId wallGraphic;
};
ecs_comp_define(ObjectComp);
ecs_comp_define(ObjectUnitComp);

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
    break;
  }
  diag_crash_msg("Unsupported faction");
}

static EcsEntityId object_unit_graphic(const ObjectDatabaseComp* db, const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return db->unitAGraphic;
  case SceneFaction_B:
    return db->unitBGraphic;
  case SceneFaction_C:
  case SceneFaction_D:
    return db->unitCGraphic;
  case SceneFaction_Count:
  case SceneFaction_None:
    break;
  }
  diag_crash_msg("Unsupported faction");
}

static EcsEntityId object_unit_behavior(const ObjectDatabaseComp* db, const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
  case SceneFaction_B:
  case SceneFaction_D:
    return db->unitBehaviorAuto;
  case SceneFaction_C:
    return db->unitBehaviorManual;
  case SceneFaction_Count:
  case SceneFaction_None:
    break;
  }
  diag_crash_msg("Unsupported faction");
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
      .unitAGraphic       = asset_lookup(world, assets, string_lit("graphics/sandbox/swat_a.gra")),
      .unitBGraphic       = asset_lookup(world, assets, string_lit("graphics/sandbox/swat_b.gra")),
      .unitCGraphic       = asset_lookup(world, assets, string_lit("graphics/sandbox/swat_c.gra")),
      .vfxMuzzleFlash     = asset_lookup(world, assets, string_lit("vfx/sandbox/muzzleflash.vfx")),
      .vfxProjectile      = asset_lookup(world, assets, string_lit("vfx/sandbox/projectile.vfx")),
      .vfxImpact          = asset_lookup(world, assets, string_lit("vfx/sandbox/impact.vfx")),
      .unitBehaviorAuto   = asset_lookup(world, assets, string_lit("behaviors/unit-auto.bt")),
      .unitBehaviorManual = asset_lookup(world, assets, string_lit("behaviors/unit-manual.bt")),
      .wallGraphic        = asset_lookup(world, assets, string_lit("graphics/sandbox/wall.gra")));
}

ecs_module_init(sandbox_object_module) {
  ecs_register_comp(ObjectDatabaseComp);
  ecs_register_comp_empty(ObjectComp);
  ecs_register_comp_empty(ObjectUnitComp);

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

  const EcsEntityId graphic  = object_unit_graphic(db, faction);
  const EcsEntityId behavior = object_unit_behavior(db, faction);
  const SceneLayer  layer    = object_unit_layer(faction);

  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_empty_t(world, e, ObjectUnitComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = graphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rotation);
  scene_nav_add_agent(world, e);
  ecs_world_add_t(world, e, SceneLocomotionComp, .maxSpeed = g_speed, .radius = 0.4f);
  ecs_world_add_t(world, e, SceneHealthComp, .norm = 1.0f, .max = 100.0f);
  ecs_world_add_t(world, e, SceneDamageComp);
  ecs_world_add_t(world, e, SceneFactionComp, .id = faction);
  ecs_world_add_t(world, e, SceneTargetFinderComp);
  ecs_world_add_t(
      world,
      e,
      SceneWeaponComp,
      .animFire    = string_hash_lit("fire"),
      .jointOrigin = string_hash_lit("muzzle"),
      .vfxFire     = db->vfxMuzzleFlash,
      .vfxImpact   = db->vfxImpact,
      .projectile  = {
          .vfx            = db->vfxProjectile,
          .delay          = time_milliseconds(25),
          .speed          = 50.0f,
          .damage         = 5.0f,
          .spreadAngleMax = 2.5f,
      });
  ecs_world_add_t(world, e, SceneAttackComp, .weaponName = string_hash_lit("AssaultRifle"));
  ecs_world_add_t(world, e, SceneTagComp, .tags = SceneTags_Default | SceneTags_Unit);
  scene_collision_add_capsule(world, e, g_capsule, layer);
  scene_brain_add(world, e, behavior);
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
  ecs_world_add_t(world, e, SceneTagComp, .tags = SceneTags_Default);
  scene_collision_add_box(world, e, g_box, SceneLayer_Environment);
  scene_nav_add_blocker(world, e);
  return e;
}
