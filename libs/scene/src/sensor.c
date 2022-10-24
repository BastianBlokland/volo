#include "ai_blackboard.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_nav.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"

// clang-format off

static StringHash g_blackboardKeyTime,
                  g_blackboardKeyEntity,
                  g_blackboardKeyPosition,
                  g_blackboardKeyHealth,
                  g_blackboardKeyFaction,
                  g_blackboardKeyNavArrived,
                  g_blackboardKeyTargetEntity,
                  g_blackboardKeyTargetPosition,
                  g_blackboardKeyTargetDist,
                  g_blackboardKeyTargetLos;

// clang-format on

ecs_view_define(SensorGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(BrainView) {
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
    AiBlackboard* bb = scene_brain_blackboard_mutable(brain);

    ai_blackboard_set(bb, g_blackboardKeyTime, ai_value_time(timeComp->time));
    ai_blackboard_set(bb, g_blackboardKeyEntity, ai_value_entity(entity));

    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    if (transform) {
      ai_blackboard_set(bb, g_blackboardKeyPosition, ai_value_vector(transform->position));
    }

    const SceneHealthComp* health = ecs_view_read_t(itr, SceneHealthComp);
    if (health) {
      ai_blackboard_set(bb, g_blackboardKeyHealth, ai_value_f64(health->norm));
    }

    const SceneFactionComp* faction = ecs_view_read_t(itr, SceneFactionComp);
    if (faction) {
      ai_blackboard_set(bb, g_blackboardKeyFaction, ai_value_f64(faction->id));
    }

    const SceneNavAgentComp* navAgent = ecs_view_read_t(itr, SceneNavAgentComp);
    if (navAgent) {
      if (navAgent->flags & SceneNavAgent_Traveling) {
        ai_blackboard_set_none(bb, g_blackboardKeyNavArrived);
      } else {
        ai_blackboard_set(bb, g_blackboardKeyNavArrived, ai_value_vector(navAgent->target));
      }
    }

    const SceneTargetFinderComp* targetFinder = ecs_view_read_t(itr, SceneTargetFinderComp);
    if (targetFinder && targetFinder->target) {
      const f64  distToTarget = math_sqrt_f64(targetFinder->targetDistSqr);
      const bool los          = (targetFinder->targetFlags & SceneTarget_LineOfSight) != 0;

      ai_blackboard_set(bb, g_blackboardKeyTargetEntity, ai_value_entity(targetFinder->target));
      ai_blackboard_set(
          bb, g_blackboardKeyTargetPosition, ai_value_vector(targetFinder->targetPosition));
      ai_blackboard_set(bb, g_blackboardKeyTargetDist, ai_value_f64(distToTarget));
      ai_blackboard_set(bb, g_blackboardKeyTargetLos, ai_value_bool(los));
    } else {
      ai_blackboard_set_none(bb, g_blackboardKeyTargetEntity);
      ai_blackboard_set_none(bb, g_blackboardKeyTargetPosition);
      ai_blackboard_set_none(bb, g_blackboardKeyTargetDist);
      ai_blackboard_set_none(bb, g_blackboardKeyTargetLos);
    }
  }
}

ecs_module_init(scene_sensor_module) {
  g_blackboardKeyTime           = stringtable_add(g_stringtable, string_lit("global-time"));
  g_blackboardKeyEntity         = stringtable_add(g_stringtable, string_lit("self-entity"));
  g_blackboardKeyPosition       = stringtable_add(g_stringtable, string_lit("self-position"));
  g_blackboardKeyHealth         = stringtable_add(g_stringtable, string_lit("self-health"));
  g_blackboardKeyFaction        = stringtable_add(g_stringtable, string_lit("self-faction"));
  g_blackboardKeyNavArrived     = stringtable_add(g_stringtable, string_lit("self-nav-arrived"));
  g_blackboardKeyTargetEntity   = stringtable_add(g_stringtable, string_lit("target-entity"));
  g_blackboardKeyTargetPosition = stringtable_add(g_stringtable, string_lit("target-position"));
  g_blackboardKeyTargetDist     = stringtable_add(g_stringtable, string_lit("target-dist"));
  g_blackboardKeyTargetLos      = stringtable_add(g_stringtable, string_lit("target-los"));

  ecs_register_view(SensorGlobalView);
  ecs_register_view(BrainView);

  ecs_register_system(SceneSensorUpdateSys, ecs_view_id(SensorGlobalView), ecs_view_id(BrainView));

  ecs_parallel(SceneSensorUpdateSys, 2);
}
