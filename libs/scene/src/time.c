#include "core_time.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_time.h"

ecs_comp_define_public(SceneTimeComp);
ecs_comp_define_public(SceneTimeSettingsComp);
ecs_comp_define(SceneTimePrivateComp) { TimeSteady lastTime; };

ecs_view_define(TimeUpdateView) {
  ecs_access_write(SceneTimeComp);
  ecs_access_read(SceneTimeSettingsComp);
  ecs_access_write(SceneTimePrivateComp);
}

static void scene_time_create(EcsWorld* world) {
  const EcsEntityId entity = ecs_world_global(world);
  ecs_world_add_t(world, entity, SceneTimeComp);
  ecs_world_add_t(world, entity, SceneTimeSettingsComp, .scale = 1.0f);
  ecs_world_add_t(world, entity, SceneTimePrivateComp, .lastTime = time_steady_clock());
}

ecs_system_define(SceneTimeUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, TimeUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    scene_time_create(world);
    return;
  }
  SceneTimeComp*               time         = ecs_view_write_t(globalItr, SceneTimeComp);
  const SceneTimeSettingsComp* timeSettings = ecs_view_read_t(globalItr, SceneTimeSettingsComp);
  SceneTimePrivateComp*        timePrivate  = ecs_view_write_t(globalItr, SceneTimePrivateComp);

  const TimeSteady   newSteadyTime   = time_steady_clock();
  const TimeDuration deltaTime       = time_steady_duration(timePrivate->lastTime, newSteadyTime);
  const TimeDuration deltaTimeScaled = (TimeDuration)(deltaTime * timeSettings->scale);

  ++time->ticks;
  time->time += deltaTimeScaled;
  time->realTime += deltaTime;
  time->delta           = deltaTimeScaled;
  time->realDelta       = deltaTime;
  timePrivate->lastTime = newSteadyTime;
}

ecs_module_init(scene_time_module) {
  ecs_register_comp(SceneTimeComp);
  ecs_register_comp(SceneTimeSettingsComp);
  ecs_register_comp(SceneTimePrivateComp);

  ecs_register_view(TimeUpdateView);

  ecs_register_system(SceneTimeUpdateSys, ecs_view_id(TimeUpdateView));
  ecs_order(SceneTimeUpdateSys, SceneOrder_TimeUpdate);
}

f32 scene_time_seconds(const SceneTimeComp* time) { return time->time / (f32)time_second; }

f32 scene_delta_seconds(const SceneTimeComp* time) { return time->delta / (f32)time_second; }
