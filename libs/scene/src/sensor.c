#include "ai_blackboard.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_health.h"
#include "scene_target.h"
#include "scene_time.h"

static StringHash g_blackboardKeyTime, g_blackboardKeyHealth, g_blackboardKeyTarget,
    g_blackboardKeyTargetDist;

ecs_view_define(SensorGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(BrainView) {
  ecs_access_maybe_read(SceneHealthComp);
  ecs_access_maybe_read(SceneTargetFinderComp);
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
    SceneBrainComp* brain = ecs_view_write_t(itr, SceneBrainComp);
    AiBlackboard*   bb    = scene_brain_blackboard_mutable(brain);

    ai_blackboard_set_time(bb, g_blackboardKeyTime, timeComp->time);

    const SceneHealthComp* health = ecs_view_read_t(itr, SceneHealthComp);
    if (health) {
      ai_blackboard_set_f64(bb, g_blackboardKeyHealth, health->norm);
    }

    const SceneTargetFinderComp* targetFinder = ecs_view_read_t(itr, SceneTargetFinderComp);
    if (targetFinder) {
      ai_blackboard_set_entity(bb, g_blackboardKeyTarget, targetFinder->target);
      const f64 distToTarget = math_sqrt_f64(targetFinder->targetDistSqr);
      ai_blackboard_set_f64(bb, g_blackboardKeyTargetDist, distToTarget);
    }
  }
}

ecs_module_init(scene_sensor_module) {
  g_blackboardKeyTime       = stringtable_add(g_stringtable, string_lit("time"));
  g_blackboardKeyHealth     = stringtable_add(g_stringtable, string_lit("health"));
  g_blackboardKeyTarget     = stringtable_add(g_stringtable, string_lit("target"));
  g_blackboardKeyTargetDist = stringtable_add(g_stringtable, string_lit("target-dist"));

  ecs_register_view(SensorGlobalView);
  ecs_register_view(BrainView);

  ecs_register_system(SceneSensorUpdateSys, ecs_view_id(SensorGlobalView), ecs_view_id(BrainView));
}
