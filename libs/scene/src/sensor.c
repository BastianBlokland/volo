#include "ai_blackboard.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_time.h"

ecs_comp_define(SceneSensorTimeComp);

ecs_view_define(SensorGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(SensorTimeView) {
  ecs_access_with(SceneSensorTimeComp);
  ecs_access_write(SceneBrainComp);
}

ecs_system_define(SceneSensorTimeSys) {
  EcsView*     globalView = ecs_world_view_t(world, SensorGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* timeComp = ecs_view_read_t(globalItr, SceneTimeComp);

  // TODO: Compute the key hashes once.
  const StringHash timeKey = stringtable_add(g_stringtable, string_lit("time"));

  EcsView* view = ecs_world_view_t(world, SensorTimeView);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    SceneBrainComp* brain = ecs_view_write_t(itr, SceneBrainComp);
    AiBlackboard*   bb    = scene_brain_blackboard_mutable(brain);

    ai_blackboard_set_time(bb, timeKey, timeComp->time);
  }
}

ecs_module_init(scene_sensor_module) {
  ecs_register_comp_empty(SceneSensorTimeComp);

  ecs_register_view(SensorGlobalView);
  ecs_register_view(SensorTimeView);

  ecs_register_system(
      SceneSensorTimeSys, ecs_view_id(SensorGlobalView), ecs_view_id(SensorTimeView));
}
