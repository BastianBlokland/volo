#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneTimeComp) {
  TimeDuration time, realTime;   // Real time is unaffected by scaling.
  TimeDuration delta, realDelta; // Real delta is unaffected by scaling.
  u64          ticks;
};

ecs_comp_extern_public(SceneTimeSettingsComp) { f32 scale; };

f32 scene_time_seconds(const SceneTimeComp*);
f32 scene_delta_seconds(const SceneTimeComp*);
