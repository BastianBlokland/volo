#include "core_annotation.h"
#include "ecs_world.h"
#include "scene_lifetime.h"
#include "scene_time.h"

ecs_comp_define_public(SceneLifetimeOwnerComp);
ecs_comp_define_public(SceneLifetimeDurationComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }
ecs_view_define(LifetimeOwnerView) { ecs_access_read(SceneLifetimeOwnerComp); }
ecs_view_define(LifetimeDurationView) { ecs_access_write(SceneLifetimeDurationComp); }

ecs_system_define(SceneLifetimeOwnerSys) {
  EcsView* lifetimeView = ecs_world_view_t(world, LifetimeOwnerView);
  for (EcsIterator* itr = ecs_view_itr(lifetimeView); ecs_view_walk(itr);) {
    const SceneLifetimeOwnerComp* lifetime = ecs_view_read_t(itr, SceneLifetimeOwnerComp);
    if (!ecs_world_exists(world, lifetime->owner)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_system_define(SceneLifetimeDurationSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* lifetimeView = ecs_world_view_t(world, LifetimeDurationView);
  for (EcsIterator* itr = ecs_view_itr(lifetimeView); ecs_view_walk(itr);) {
    SceneLifetimeDurationComp* lifetime = ecs_view_write_t(itr, SceneLifetimeDurationComp);
    if ((lifetime->duration -= time->delta) < 0) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_module_init(scene_lifetime_module) {
  ecs_register_comp(SceneLifetimeOwnerComp);
  ecs_register_comp(SceneLifetimeDurationComp);

  ecs_register_view(GlobalView);
  ecs_register_view(LifetimeOwnerView);
  ecs_register_view(LifetimeDurationView);

  ecs_register_system(SceneLifetimeOwnerSys, ecs_view_id(LifetimeOwnerView));
  ecs_register_system(
      SceneLifetimeDurationSys, ecs_view_id(GlobalView), ecs_view_id(LifetimeDurationView));
}
