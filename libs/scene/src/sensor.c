#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_brain.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"

// clang-format off

static StringHash g_brainKeyTime,
                  g_brainKeyEntity,
                  g_brainKeyPosition,
                  g_brainKeyHealth,
                  g_brainKeyFaction,
                  g_brainKeyNavArrived,
                  g_brainKeyAttacking,
                  g_brainKeyTargetEntity,
                  g_brainKeyTargetPos,
                  g_brainKeyTargetDist,
                  g_brainKeyTargetLos;

// clang-format on

ecs_view_define(SensorGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(BrainView) {
  ecs_access_maybe_read(SceneAttackComp);
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneHealthComp);
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneTargetFinderComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_write(SceneBrainComp);
}

ecs_system_define(SceneSensorUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, SensorGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* timeComp = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* view = ecs_world_view_t(world, BrainView);
  for (EcsIterator* itr = ecs_view_itr_step(view, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    SceneBrainComp*   brain  = ecs_view_write_t(itr, SceneBrainComp);
    if (scene_brain_flags(brain) & SceneBrainFlags_PauseSensors) {
      continue;
    }

    scene_brain_set(brain, g_brainKeyTime, script_time(timeComp->time));
    scene_brain_set(brain, g_brainKeyEntity, script_entity(entity));

    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    if (transform) {
      scene_brain_set(brain, g_brainKeyPosition, script_vector3(transform->position));
    }

    const SceneHealthComp* health = ecs_view_read_t(itr, SceneHealthComp);
    if (health) {
      scene_brain_set(brain, g_brainKeyHealth, script_number(health->norm));
    }

    const SceneFactionComp* faction = ecs_view_read_t(itr, SceneFactionComp);
    if (faction) {
      scene_brain_set(brain, g_brainKeyFaction, script_number(faction->id));
    }

    const SceneNavAgentComp* navAgent = ecs_view_read_t(itr, SceneNavAgentComp);
    if (navAgent) {
      if (navAgent->flags & SceneNavAgent_Traveling) {
        scene_brain_set_null(brain, g_brainKeyNavArrived);
      } else {
        scene_brain_set(brain, g_brainKeyNavArrived, script_vector3(navAgent->target));
      }
    }

    const SceneAttackComp* attack = ecs_view_read_t(itr, SceneAttackComp);
    if (attack) {
      const bool isAttacking = (attack->flags & SceneAttackFlags_Firing) != 0;
      scene_brain_set(brain, g_brainKeyAttacking, script_bool(isAttacking));
    }

    const SceneTargetFinderComp* targetFinder = ecs_view_read_t(itr, SceneTargetFinderComp);
    if (targetFinder && targetFinder->target) {
      const bool los = (targetFinder->flags & SceneTarget_LineOfSight) != 0;

      scene_brain_set(brain, g_brainKeyTargetEntity, script_entity(targetFinder->target));
      scene_brain_set(brain, g_brainKeyTargetPos, script_vector3(targetFinder->targetPosition));
      scene_brain_set(brain, g_brainKeyTargetDist, script_number(targetFinder->targetDistance));
      scene_brain_set(brain, g_brainKeyTargetLos, script_bool(los));
    } else {
      scene_brain_set_null(brain, g_brainKeyTargetEntity);
      scene_brain_set_null(brain, g_brainKeyTargetPos);
      scene_brain_set_null(brain, g_brainKeyTargetDist);
      scene_brain_set_null(brain, g_brainKeyTargetLos);
    }
  }
}

ecs_module_init(scene_sensor_module) {
  g_brainKeyTime         = stringtable_add(g_stringtable, string_lit("global_time"));
  g_brainKeyEntity       = stringtable_add(g_stringtable, string_lit("self_entity"));
  g_brainKeyPosition     = stringtable_add(g_stringtable, string_lit("self_position"));
  g_brainKeyHealth       = stringtable_add(g_stringtable, string_lit("self_health"));
  g_brainKeyFaction      = stringtable_add(g_stringtable, string_lit("self_faction"));
  g_brainKeyNavArrived   = stringtable_add(g_stringtable, string_lit("self_nav_arrived"));
  g_brainKeyAttacking    = stringtable_add(g_stringtable, string_lit("self_attacking"));
  g_brainKeyTargetEntity = stringtable_add(g_stringtable, string_lit("target_entity"));
  g_brainKeyTargetPos    = stringtable_add(g_stringtable, string_lit("target_position"));
  g_brainKeyTargetDist   = stringtable_add(g_stringtable, string_lit("target_distance"));
  g_brainKeyTargetLos    = stringtable_add(g_stringtable, string_lit("target_los"));

  ecs_register_view(SensorGlobalView);
  ecs_register_view(BrainView);

  ecs_register_system(SceneSensorUpdateSys, ecs_view_id(SensorGlobalView), ecs_view_id(BrainView));

  ecs_order(SceneSensorUpdateSys, SceneOrder_SensorUpdate);
  ecs_parallel(SceneSensorUpdateSys, 2);
}
