#include "asset_manager.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_brain.h"
#include "scene_collision.h"
#include "scene_health.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_prefab.h"
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
  (void)db;

  const ScenePrefabSpec spec = {
      .prefabId = string_hash_lit("UnitRifle"),
      .position = pos,
      .rotation = geo_quat_look(geo_backward, geo_up),
      .faction  = SceneFaction_A,
  };
  const EcsEntityId e = scene_prefab_spawn(world, &spec);
  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_empty_t(world, e, ObjectUnitComp);
  return e;
}

EcsEntityId
object_spawn_unit_ai(EcsWorld* world, const ObjectDatabaseComp* db, const GeoVector pos) {
  (void)db;

  const ScenePrefabSpec spec = {
      .prefabId = string_hash_lit("UnitMelee"),
      .position = pos,
      .rotation = geo_quat_look(geo_backward, geo_up),
      .faction  = SceneFaction_B,
  };
  const EcsEntityId e = scene_prefab_spawn(world, &spec);
  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_empty_t(world, e, ObjectUnitComp);
  return e;
}

EcsEntityId object_spawn_wall(
    EcsWorld* world, const ObjectDatabaseComp* db, const GeoVector pos, const GeoQuat rot) {
  (void)db;

  const ScenePrefabSpec spec = {
      .prefabId = string_hash_lit("Wall"),
      .position = pos,
      .rotation = rot,
      .faction  = SceneFaction_B,
  };
  const EcsEntityId e = scene_prefab_spawn(world, &spec);
  ecs_world_add_empty_t(world, e, ObjectComp);
  return e;
}
