#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_health.h"

ecs_comp_define_public(SceneHealthComp);

ecs_view_define(HealthView) { ecs_access_read(SceneHealthComp); }

ecs_system_define(SceneHealthUpdateSys) {
  EcsView* healthView = ecs_world_view_t(world, HealthView);
  for (EcsIterator* itr = ecs_view_itr(healthView); ecs_view_walk(itr);) {
    const SceneHealthComp* health = ecs_view_read_t(itr, SceneHealthComp);
    if (health->norm <= 0.0f) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_module_init(scene_health_module) {
  ecs_register_comp(SceneHealthComp);

  ecs_register_view(HealthView);

  ecs_register_system(SceneHealthUpdateSys, ecs_view_id(HealthView));
}

void scene_health_damage(SceneHealthComp* health, const f32 amount) {
  diag_assert(amount >= 0.0f);

  const f32 damageNorm = health->max > 0.0f ? (amount / health->max) : 1.0f;
  health->norm         = math_max(0, health->norm - damageNorm);
}
