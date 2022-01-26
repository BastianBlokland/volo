#include "core_annotation.h"
#include "ecs_world.h"
#include "scene_lifetime.h"

ecs_comp_define_public(SceneLifetimeOwnerComp);

ecs_view_define(LifetimeOwnerView) { ecs_access_read(SceneLifetimeOwnerComp); }

ecs_system_define(SceneLifetimeOwnerSys) {
  EcsView* lifetimeView = ecs_world_view_t(world, LifetimeOwnerView);
  for (EcsIterator* itr = ecs_view_itr(lifetimeView); ecs_view_walk(itr);) {
    const SceneLifetimeOwnerComp* lifetime = ecs_view_read_t(itr, SceneLifetimeOwnerComp);
    if (!ecs_world_exists(world, lifetime->owner)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_module_init(scene_lifetime_module) {
  ecs_register_comp(SceneLifetimeOwnerComp);

  ecs_register_view(LifetimeOwnerView);

  ecs_register_system(SceneLifetimeOwnerSys, ecs_view_id(LifetimeOwnerView));
}
