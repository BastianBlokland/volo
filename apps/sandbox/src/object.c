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
  EcsEntityId unitPlayerGraphic, unitPlayerBehavior;
  EcsEntityId unitAiGraphic, unitAiBehavior;
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
  default:
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
  AssetManagerComp* man = ecs_view_write_t(globalItr, AssetManagerComp);

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      ObjectDatabaseComp,
      .unitPlayerGraphic  = asset_lookup(world, man, string_lit("graphics/sandbox/swat_a.gra")),
      .unitPlayerBehavior = asset_lookup(world, man, string_lit("behaviors/unit-ranged-manual.bt")),
      .unitAiGraphic      = asset_lookup(world, man, string_lit("graphics/sandbox/maynard.gra")),
      .unitAiBehavior     = asset_lookup(world, man, string_lit("behaviors/unit-melee-auto.bt")),
      .wallGraphic        = asset_lookup(world, man, string_lit("graphics/sandbox/wall.gra")));
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

  switch (faction) {
  case SceneFaction_A:
    return object_spawn_unit_player(world, db, pos);
  case SceneFaction_B:
    return object_spawn_unit_ai(world, db, pos);
  default:
    diag_crash_msg("Unsupported faction");
  }
}

EcsEntityId
object_spawn_unit_player(EcsWorld* world, const ObjectDatabaseComp* db, const GeoVector pos) {
  static const SceneCollisionCapsule g_capsule = {
      .offset = {0, 0.3f, 0},
      .radius = 0.3f,
      .height = 1.2f,
  };

  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoQuat     rotation = geo_quat_look(geo_backward, geo_up);

  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_empty_t(world, e, ObjectUnitComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = db->unitPlayerGraphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rotation);
  scene_nav_add_agent(world, e);
  ecs_world_add_t(world, e, SceneLocomotionComp, .maxSpeed = 4.0f, .radius = 0.4f);
  ecs_world_add_t(world, e, SceneHealthComp, .norm = 1.0f, .max = 100.0f);
  ecs_world_add_t(world, e, SceneDamageComp);
  ecs_world_add_t(world, e, SceneFactionComp, .id = SceneFaction_A);
  ecs_world_add_t(world, e, SceneTargetFinderComp);
  ecs_world_add_t(world, e, SceneAttackComp, .weaponName = string_hash_lit("AssaultRifle"));
  ecs_world_add_t(world, e, SceneTagComp, .tags = SceneTags_Default | SceneTags_Unit);
  scene_collision_add_capsule(world, e, g_capsule, object_unit_layer(SceneFaction_A));
  scene_brain_add(world, e, db->unitPlayerBehavior);
  return e;
}

EcsEntityId
object_spawn_unit_ai(EcsWorld* world, const ObjectDatabaseComp* db, const GeoVector pos) {
  static const SceneCollisionCapsule g_capsule = {
      .offset = {0, 0.3f, 0},
      .radius = 0.3f,
      .height = 1.1f,
  };

  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoQuat     rotation = geo_quat_look(geo_backward, geo_up);

  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_empty_t(world, e, ObjectUnitComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = db->unitAiGraphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rotation);
  scene_nav_add_agent(world, e);
  ecs_world_add_t(world, e, SceneLocomotionComp, .maxSpeed = 4.5f, .radius = 0.35f);
  ecs_world_add_t(world, e, SceneHealthComp, .norm = 1.0f, .max = 150.0f);
  ecs_world_add_t(world, e, SceneDamageComp);
  ecs_world_add_t(world, e, SceneFactionComp, .id = SceneFaction_B);
  ecs_world_add_t(world, e, SceneTargetFinderComp);
  ecs_world_add_t(world, e, SceneAttackComp, .weaponName = string_hash_lit("Melee"));
  ecs_world_add_t(world, e, SceneTagComp, .tags = SceneTags_Default | SceneTags_Unit);
  scene_collision_add_capsule(world, e, g_capsule, object_unit_layer(SceneFaction_B));
  scene_brain_add(world, e, db->unitAiBehavior);
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
