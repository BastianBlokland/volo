#include "ecs_world.h"
#include "scene_collision.h"

ecs_comp_define_public(SceneCollisionComp);

ecs_module_init(scene_collision_module) { ecs_register_comp(SceneCollisionComp); }

void scene_collision_add_capsule(
    EcsWorld* world, const EcsEntityId entity, const SceneCollisionCapsule capsule) {
  ecs_world_add_t(world, entity, SceneCollisionComp, .data_capsule = capsule);
}
