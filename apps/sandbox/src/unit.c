#include "asset_manager.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_renderable.h"
#include "scene_transform.h"

#include "unit_internal.h"

static const String                g_unitGraphic = string_static("graphics/sandbox/vanguard.gra");
static const SceneCollisionCapsule g_unitCapsule = {
    .offset = {0, 0.3f, 0},
    .radius = 0.3f,
    .height = 1.2f,
};
static const f32 g_unitSpeed = 1.0f;

ecs_comp_define(UnitDatabaseComp) { EcsEntityId unitGraphic; };
ecs_comp_define(UnitComp);

ecs_view_define(GlobalInitView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_without(UnitDatabaseComp);
}

ecs_system_define(UnitDatabaseInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalInitView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Already initialized or dependencies not ready.
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      UnitDatabaseComp,
      .unitGraphic = asset_lookup(world, assets, g_unitGraphic));
}

ecs_module_init(sandbox_unit_module) {
  ecs_register_comp(UnitDatabaseComp);
  ecs_register_comp_empty(UnitComp);

  ecs_register_view(GlobalInitView);

  ecs_register_system(UnitDatabaseInitSys, ecs_view_id(GlobalInitView));
}

EcsEntityId unit_spawn(EcsWorld* world, const UnitDatabaseComp* db, const GeoVector position) {
  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoQuat     rotation = geo_quat_look(geo_backward, geo_up);

  ecs_world_add_empty_t(world, e, UnitComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = db->unitGraphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = position, .rotation = rotation);
  ecs_world_add_t(world, e, SceneLocomotionComp, .target = position, .speed = g_unitSpeed);
  scene_collision_add_capsule(world, e, g_unitCapsule);
  return e;
}
