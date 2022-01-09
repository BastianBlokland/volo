#include "core_time.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_time.h"

ecs_comp_define_public(SceneTimeComp);
ecs_comp_define(SceneTimePrivateComp) { TimeSteady lastTime, startTime; };

ecs_view_define(TimeUpdateView) {
  ecs_access_write(SceneTimeComp);
  ecs_access_write(SceneTimePrivateComp);
}

static void scene_time_create(EcsWorld* world) {
  const EcsEntityId entity = ecs_world_global(world);
  ecs_world_add_t(world, entity, SceneTimeComp);
  ecs_world_add_t(
      world,
      entity,
      SceneTimePrivateComp,
      .lastTime  = time_steady_clock(),
      .startTime = time_steady_clock());
}

ecs_system_define(SceneTimeUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, TimeUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    scene_time_create(world);
    return;
  }
  SceneTimeComp*        time        = ecs_view_write_t(globalItr, SceneTimeComp);
  SceneTimePrivateComp* timePrivate = ecs_view_write_t(globalItr, SceneTimePrivateComp);

  const TimeSteady   newSteadyTime = time_steady_clock();
  const TimeDuration deltaTime     = time_steady_duration(timePrivate->lastTime, newSteadyTime);

  ++time->ticks;
  time->delta           = deltaTime;
  time->time            = time_steady_duration(timePrivate->startTime, newSteadyTime);
  timePrivate->lastTime = newSteadyTime;
}

ecs_module_init(scene_time_module) {
  ecs_register_comp(SceneTimeComp);
  ecs_register_comp(SceneTimePrivateComp);

  ecs_register_view(TimeUpdateView);

  ecs_register_system(SceneTimeUpdateSys, ecs_view_id(TimeUpdateView));
  ecs_order(SceneTimeUpdateSys, SceneOrder_TimeUpdate);
}
