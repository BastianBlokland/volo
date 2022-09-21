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
                  g_blackboardKeyNavMoving,
                  g_blackboardKeyTargetEntity,
                  g_blackboardKeyTargetDist;

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
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    SceneBrainComp*   brain  = ecs_view_write_t(itr, SceneBrainComp);
    if (scene_brain_flags(brain) & SceneBrainFlags_PauseSensors) {
      continue;
    }
    AiBlackboard* bb = scene_brain_blackboard_mutable(brain);

    ai_blackboard_set_time(bb, g_blackboardKeyTime, timeComp->time);
    ai_blackboard_set_entity(bb, g_blackboardKeyEntity, entity);

    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    if (transform) {
      ai_blackboard_set_vector(bb, g_blackboardKeyPosition, transform->position);
    }

    const SceneHealthComp* health = ecs_view_read_t(itr, SceneHealthComp);
    if (health) {
      ai_blackboard_set_f64(bb, g_blackboardKeyHealth, health->norm);
    }

    const SceneFactionComp* faction = ecs_view_read_t(itr, SceneFactionComp);
    if (faction) {
      ai_blackboard_set_f64(bb, g_blackboardKeyFaction, faction->id);
    }

    const SceneNavAgentComp* navAgent = ecs_view_read_t(itr, SceneNavAgentComp);
    if (navAgent) {
      const bool moving = (navAgent->flags & SceneNavAgent_Moving) != 0;
      ai_blackboard_set_bool(bb, g_blackboardKeyNavMoving, moving);
    }

    const SceneTargetFinderComp* targetFinder = ecs_view_read_t(itr, SceneTargetFinderComp);
    if (targetFinder) {
      ai_blackboard_set_entity(bb, g_blackboardKeyTargetEntity, targetFinder->target);
      const f64 distToTarget = math_sqrt_f64(targetFinder->targetDistSqr);
      ai_blackboard_set_f64(bb, g_blackboardKeyTargetDist, distToTarget);
    }
  }
}

ecs_module_init(scene_sensor_module) {
  g_blackboardKeyTime         = stringtable_add(g_stringtable, string_lit("global-time"));
  g_blackboardKeyEntity       = stringtable_add(g_stringtable, string_lit("self-entity"));
  g_blackboardKeyPosition     = stringtable_add(g_stringtable, string_lit("self-position"));
  g_blackboardKeyHealth       = stringtable_add(g_stringtable, string_lit("self-health"));
  g_blackboardKeyFaction      = stringtable_add(g_stringtable, string_lit("self-faction"));
  g_blackboardKeyNavMoving    = stringtable_add(g_stringtable, string_lit("self-nav-moving"));
  g_blackboardKeyTargetEntity = stringtable_add(g_stringtable, string_lit("target-entity"));
  g_blackboardKeyTargetDist   = stringtable_add(g_stringtable, string_lit("target-dist"));

  ecs_register_view(SensorGlobalView);
  ecs_register_view(BrainView);

  ecs_register_system(SceneSensorUpdateSys, ecs_view_id(SensorGlobalView), ecs_view_id(BrainView));
}
