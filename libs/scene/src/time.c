#include "core_diag.h"
#include "core_math.h"
#include "core_time.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_time.h"

ecs_comp_define_public(SceneTimeComp);
ecs_comp_define_public(SceneTimeSettingsComp);
ecs_comp_define(SceneTimePrivateComp) { TimeSteady lastTime; };

static f32 time_to_seconds(const TimeDuration dur) {
  static const f64 g_toSecMul = 1.0 / (f64)time_second;
  // NOTE: Potentially can be done in 32 bit but with nano-seconds its at the edge of f32 precision.
  return (f32)((f64)dur * g_toSecMul);
}

ecs_view_define(TimeUpdateView) {
  ecs_access_write(SceneTimeComp);
  ecs_access_write(SceneTimeSettingsComp);
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
  SceneTimeComp*         time         = ecs_view_write_t(globalItr, SceneTimeComp);
  SceneTimeSettingsComp* timeSettings = ecs_view_write_t(globalItr, SceneTimeSettingsComp);
  SceneTimePrivateComp*  timePrivate  = ecs_view_write_t(globalItr, SceneTimePrivateComp);

  diag_assert_msg(timeSettings->scale >= 0.0f, "Time cannot flow backwards");

  const TimeSteady newSteadyTime = time_steady_clock();
  TimeDuration     realDelta     = time_steady_duration(timePrivate->lastTime, newSteadyTime);

  /**
   * Limit the maximum frame delta-time to avoid a huge delta when the game's process was paused.
   */
  realDelta = math_min(realDelta, time_second);

  TimeDuration delta;
  if (timeSettings->flags & SceneTimeFlags_Step) {
    delta = (TimeDuration)(time_second / 60.0f * timeSettings->scale);
    timeSettings->flags &= ~SceneTimeFlags_Step;
  } else {
    const bool isPaused       = (timeSettings->flags & SceneTimeFlags_Paused) != 0;
    const f32  effectiveScale = isPaused ? 0.0f : timeSettings->scale;
    delta                     = (TimeDuration)(realDelta * effectiveScale);
  }

  ++time->ticks;
  time->time += delta;
  time->realTime += realDelta;
  time->delta           = delta;
  time->realDelta       = realDelta;
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

f32 scene_time_seconds(const SceneTimeComp* time) { return time_to_seconds(time->time); }
f32 scene_delta_seconds(const SceneTimeComp* time) { return time_to_seconds(time->delta); }
f32 scene_real_time_seconds(const SceneTimeComp* time) { return time_to_seconds(time->realTime); }
f32 scene_real_delta_seconds(const SceneTimeComp* time) { return time_to_seconds(time->realDelta); }
