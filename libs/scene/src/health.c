#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_health.h"
#include "scene_tag.h"
#include "scene_time.h"

ecs_comp_define_public(SceneHealthComp);

static f32 health_normalize(const SceneHealthComp* health, const f32 amount) {
  return LIKELY(health->max > 0.0f) ? (amount / health->max) : 1.0f;
}

static void health_set_damaged(EcsWorld* world, const EcsEntityId entity, SceneTagComp* tagComp) {
  if (tagComp) {
    tagComp->tags |= SceneTags_Damaged;
  } else {
    scene_tag_add(world, entity, SceneTags_Default | SceneTags_Damaged);
  }
}

static void health_clear_damaged(EcsWorld* world, const EcsEntityId entity, SceneTagComp* tagComp) {
  (void)world;
  (void)entity;
  if (tagComp) {
    tagComp->tags &= ~SceneTags_Damaged;
  }
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(HealthView) {
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_write(SceneHealthComp);
}

ecs_system_define(SceneHealthUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* healthView = ecs_world_view_t(world, HealthView);
  for (EcsIterator* itr = ecs_view_itr(healthView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    SceneHealthComp*  health = ecs_view_write_t(itr, SceneHealthComp);
    SceneTagComp*     tag    = ecs_view_write_t(itr, SceneTagComp);

    const f32 damageNorm = health_normalize(health, health->damage);
    health->damage       = 0;

    if (damageNorm > 0.0f) {
      health->lastDamagedTime = time->time;
      health_set_damaged(world, entity, tag);
    } else if ((time->time - health->lastDamagedTime) > time_milliseconds(100)) {
      health_clear_damaged(world, entity, tag);
    }

    health->norm -= damageNorm;
    if (health->norm <= 0.0f) {
      health->norm = 0.0f;
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_module_init(scene_health_module) {
  ecs_register_comp(SceneHealthComp);

  ecs_register_view(GlobalView);
  ecs_register_view(HealthView);

  ecs_register_system(SceneHealthUpdateSys, ecs_view_id(GlobalView), ecs_view_id(HealthView));
}

void scene_health_damage(SceneHealthComp* health, const f32 amount) {
  diag_assert(amount >= 0.0f);
  health->damage += amount;
}
