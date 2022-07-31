#include "asset_manager.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_renderable.h"
#include "scene_transform.h"

#include "object_internal.h"

static const String g_objUnitGraphic = string_static("graphics/sandbox/vanguard.gra");
static const f32    g_objUnitSpeed   = 2.0f;
static const SceneCollisionCapsule g_objUnitCapsule = {
    .offset = {0, 0.3f, 0},
    .radius = 0.3f,
    .height = 1.2f,
};

ecs_comp_define(ObjectDatabaseComp) { EcsEntityId unitGraphic; };
ecs_comp_define(ObjectComp);

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
      .unitGraphic = asset_lookup(world, assets, g_objUnitGraphic));
}

ecs_module_init(sandbox_object_module) {
  ecs_register_comp(ObjectDatabaseComp);
  ecs_register_comp_empty(ObjectComp);

  ecs_register_view(GlobalInitView);

  ecs_register_system(ObjectDatabaseInitSys, ecs_view_id(GlobalInitView));
}

EcsEntityId
object_spawn_unit(EcsWorld* world, const ObjectDatabaseComp* db, const GeoVector position) {
  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoQuat     rotation = geo_quat_look(geo_backward, geo_up);

  ecs_world_add_empty_t(world, e, ObjectComp);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = db->unitGraphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = position, .rotation = rotation);
  ecs_world_add_t(world, e, SceneLocomotionComp, .target = position, .speed = g_objUnitSpeed);
  scene_collision_add_capsule(world, e, g_objUnitCapsule);
  return e;
}
