#pragma once
#include "core/time.h"
#include "ecs/module.h"

ecs_comp_extern_public(SceneTimeComp) {
  u64          frameIdx;         // Current frame index.
  TimeDuration time, realTime;   // Real time is unaffected by scaling.
  TimeDuration delta, realDelta; // Real delta is unaffected by scaling.
  TimeDuration levelTime;        // Time in the current level.
  u64          ticks;
};

typedef enum {
  SceneTimeFlags_None   = 0,
  SceneTimeFlags_Paused = 1 << 0,
  SceneTimeFlags_Step   = 1 << 1, // Run a single tick with a fixed delta of 16.6 ms.
} SceneTimeFlags;

ecs_comp_extern_public(SceneTimeSettingsComp) {
  SceneTimeFlags flags;
  f32            scale;
};

f32 scene_time_seconds(const SceneTimeComp*);
f32 scene_delta_seconds(const SceneTimeComp*);
f32 scene_real_time_seconds(const SceneTimeComp*);
f32 scene_real_delta_seconds(const SceneTimeComp*);
