#include "asset/manager.h"
#include "core/alloc.h"
#include "core/array.h"
#include "core/dynarray.h"
#include "ecs/entity.h"
#include "ecs/module.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "log/logger.h"
#include "scene/creator.h"
#include "scene/lifetime.h"
#include "scene/mission.h"
#include "scene/sound.h"
#include "scene/time.h"

typedef enum {
  SceneMissionSound_Progress,
  SceneMissionSound_Success,
  SceneMissionSound_Fail,

  SceneMissionSound_Count,
} SceneMissionSound;

static const String g_sceneMissionSoundIds[SceneMissionSound_Count] = {
    [SceneMissionSound_Progress] = string_static("external/sound/builtin/objective-success-01.wav"),
    [SceneMissionSound_Success]  = string_static("external/sound/builtin/objective-success-02.wav"),
    [SceneMissionSound_Fail]     = string_static("external/sound/builtin/objective-fail-02.wav"),
};
static const f32 g_sceneMissionSoundGain[SceneMissionSound_Count] = {
    [SceneMissionSound_Progress] = 0.6f,
    [SceneMissionSound_Success]  = 0.7f,
    [SceneMissionSound_Fail]     = 0.6f,
};
static const f32 g_sceneMissionSoundPitch[SceneMissionSound_Count] = {
    [SceneMissionSound_Progress] = 1.0f,
    [SceneMissionSound_Success]  = 1.0f,
    [SceneMissionSound_Fail]     = 0.8f,
};
static const TimeDuration g_sceneMissionSoundDuration[SceneMissionSound_Count] = {
    [SceneMissionSound_Progress] = time_second,
    [SceneMissionSound_Success]  = time_seconds(5),
    [SceneMissionSound_Fail]     = time_seconds(3),
};

ecs_comp_define(SceneMissionComp) {
  StringHash        name; // Localization key.
  SceneMissionState state;
  TimeDuration      startTime;  // -1 until available.
  TimeDuration      endTime;    // -1 until available.
  EcsEntityId       instigator; // Entity that began the mission.
  DynArray          objectives; // SceneMissionObjective[].
  TimeDuration      lastSoundTime[SceneMissionSound_Count];

  EcsEntityId soundAssets[SceneMissionSound_Count];
};

static void ecs_destruct_mission(void* data) {
  SceneMissionComp* comp = data;
  dynarray_destroy(&comp->objectives);
}

static SceneMissionComp* mission_init(EcsWorld* world, AssetManagerComp* assets) {
  SceneMissionComp* mission = ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneMissionComp,
      .objectives = dynarray_create_t(g_allocHeap, SceneObjective, 32));

  for (SceneMissionSound snd = 0; snd != SceneMissionSound_Count; ++snd) {
    mission->soundAssets[snd] = asset_lookup(world, assets, g_sceneMissionSoundIds[snd]);
  }

  return mission;
}

static void mission_sound_play(
    EcsWorld*               world,
    SceneMissionComp*       mission,
    const SceneTimeComp*    time,
    const SceneMissionSound snd) {

  if ((time->realTime - mission->lastSoundTime[snd]) < time_milliseconds(150)) {
    return; // Avoid spamming sounds.
  }
  mission->lastSoundTime[snd] = time->realTime;

  const EcsEntityId  asset = mission->soundAssets[snd];
  const f32          gain  = g_sceneMissionSoundGain[snd];
  const f32          pitch = g_sceneMissionSoundPitch[snd];
  const TimeDuration dur   = g_sceneMissionSoundDuration[snd];
  const EcsEntityId  e     = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = dur);
  ecs_world_add_t(world, e, SceneSoundComp, .asset = asset, .gain = gain, .pitch = pitch);
  ecs_world_add_t(world, e, SceneCreatorComp, .creator = mission->instigator);
}

static void mission_sound_end_play(
    EcsWorld*               world,
    SceneMissionComp*       mission,
    const SceneTimeComp*    time,
    const SceneMissionState state) {
  SceneMissionSound snd;
  if (state == SceneMissionState_Success) {
    snd = SceneMissionSound_Success;
  } else {
    snd = SceneMissionSound_Fail;
  }
  mission_sound_play(world, mission, time, snd);
}

static const SceneObjective* obj_get(const SceneMissionComp* m, const SceneObjectiveId id) {
  dynarray_for_t(&m->objectives, SceneObjective, obj) {
    if (obj->id == id) {
      return obj;
    }
  }
  return null;
}

static SceneObjective* obj_get_mut(SceneMissionComp* m, const SceneObjectiveId id) {
  return (SceneObjective*)obj_get(m, id);
}

static void
obj_update(EcsWorld* world, SceneMissionComp* m, SceneObjective* obj, const SceneTimeComp* time) {
  if (obj->startTime < 0) {
    obj->startTime = time->time;
  }
  if (obj->progressUpdated) {
    mission_sound_play(world, m, time, SceneMissionSound_Progress);
    obj->progressUpdated = false;
  }
  if (obj->state != SceneMissionState_Active) {
    if (obj->endTime < 0) {
      obj->endTime = time->time;
      mission_sound_end_play(world, m, time, obj->state);
    }
    return; // Objective has ended.
  }
  if (m->state != SceneMissionState_Active) {
    return; // Mission has ended.
  }
  const TimeDuration timeElapsed = time->time - obj->startTime;
  if (obj->timeoutDuration > 0 && timeElapsed >= obj->timeoutDuration) {
    obj->endTime = obj->startTime + obj->timeoutDuration;
    obj->state   = obj->timeoutResult;
    mission_sound_end_play(world, m, time, obj->state);
  }
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_maybe_write(SceneMissionComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(SceneMissionUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  AssetManagerComp*    assets  = ecs_view_write_t(globalItr, AssetManagerComp);
  const SceneTimeComp* time    = ecs_view_read_t(globalItr, SceneTimeComp);
  SceneMissionComp*    mission = ecs_view_write_t(globalItr, SceneMissionComp);
  if (UNLIKELY(!mission)) {
    mission = mission_init(world, assets);
  }

  dynarray_for_t(&mission->objectives, SceneObjective, obj) {
    obj_update(world, mission, obj, time);
  }
  if (mission->state != SceneMissionState_Idle && mission->startTime < 0) {
    mission->startTime = time->time;
  }
  if (mission->state > SceneMissionState_Active && mission->endTime < 0) {
    mission->endTime = time->time;
    mission_sound_end_play(world, mission, time, mission->state);
  }
}

ecs_module_init(scene_mission_module) {
  ecs_register_comp(SceneMissionComp, .destructor = ecs_destruct_mission);

  ecs_register_view(UpdateGlobalView);

  ecs_register_system(SceneMissionUpdateSys, ecs_view_id(UpdateGlobalView));
}

String scene_mission_state_str(const SceneMissionState state) {
  static const String g_names[] = {
      string_static("Idle"),
      string_static("Active"),
      string_static("Success"),
      string_static("Fail"),
  };
  ASSERT(array_elems(g_names) == SceneMissionState_Count, "Incorrect number of names");
  return g_names[state];
}

String scene_mission_err_str(const SceneMissionErr err) {
  static const String g_names[] = {
      string_static("None"),
      string_static("NotActive"),
      string_static("AlreadyActive"),
      string_static("InvalidResult"),
      string_static("InvalidObjective"),
  };
  ASSERT(array_elems(g_names) == SceneMissionErr_Count, "Incorrect number of names");
  return g_names[err];
}

void scene_mission_clear(SceneMissionComp* m) {
  m->state      = SceneMissionState_Idle;
  m->name       = 0;
  m->startTime  = -1;
  m->endTime    = -1;
  m->instigator = ecs_entity_invalid;
  dynarray_clear(&m->objectives);
}

SceneMissionErr
scene_mission_begin(SceneMissionComp* m, const StringHash name, const EcsEntityId instigator) {
  if (UNLIKELY(m->state == SceneMissionState_Active)) {
    return SceneMissionErr_AlreadyActive;
  }
  scene_mission_clear(m);

  log_i("Mission begin");

  m->name       = name;
  m->state      = SceneMissionState_Active;
  m->instigator = instigator;
  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_end(SceneMissionComp* m, const SceneMissionState result) {
  if (UNLIKELY(m->state != SceneMissionState_Active)) {
    return SceneMissionErr_NotActive;
  }
  if (UNLIKELY(result != SceneMissionState_Success && result != SceneMissionState_Fail)) {
    return SceneMissionErr_InvalidResult;
  }

  log_i("Mission end", log_param("result", fmt_text(scene_mission_state_str(result))));

  m->state = result;
  return SceneMissionErr_None;
}

SceneMissionState scene_mission_state(const SceneMissionComp* m) { return m->state; }
StringHash        scene_mission_name(const SceneMissionComp* m) { return m->name; }

TimeDuration scene_mission_time(const SceneMissionComp* m, const SceneTimeComp* time) {
  if (m->endTime >= 0) {
    return m->endTime - m->startTime;
  }
  return time->time - m->startTime;
}

TimeDuration scene_mission_time_ended(const SceneMissionComp* m, const SceneTimeComp* time) {
  if (m->endTime < 0) {
    return 0;
  }
  return time->time - m->endTime;
}

SceneMissionErr
scene_mission_obj_begin(SceneMissionComp* m, const SceneObjectiveId id, const StringHash nameLoc) {
  if (UNLIKELY(m->state != SceneMissionState_Active)) {
    return SceneMissionErr_NotActive;
  }
  if (UNLIKELY(obj_get(m, id))) {
    return SceneMissionErr_InvalidObjective; // Id already used.
  }

  log_i("Objective begin", log_param("id", fmt_int(id)));

  SceneObjective* obj = dynarray_push_t(&m->objectives, SceneObjective);

  *obj = (SceneObjective){
      .id        = id,
      .nameLoc   = nameLoc,
      .state     = SceneMissionState_Active,
      .goal      = -1.0f,
      .progress  = -1.0f,
      .startTime = -1,
      .endTime   = -1,
  };

  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_obj_goal(
    SceneMissionComp* m, const SceneObjectiveId id, const f32 goal, const f32 progress) {
  if (UNLIKELY(m->state != SceneMissionState_Active)) {
    return SceneMissionErr_NotActive;
  }
  SceneObjective* obj = obj_get_mut(m, id);
  if (UNLIKELY(!obj || obj->state != SceneMissionState_Active)) {
    return SceneMissionErr_InvalidObjective;
  }
  if (obj->goal == goal && obj->progress == progress) {
    return SceneMissionErr_None;
  }

  log_d(
      "Objective update goal",
      log_param("id", fmt_int(id)),
      log_param("goal", fmt_float(goal)),
      log_param("progress", fmt_float(progress)));

  if (obj->progress >= 0.0f && obj->progress != progress) {
    obj->progressUpdated = true;
  }

  obj->goal     = goal;
  obj->progress = progress;
  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_obj_timeout(
    SceneMissionComp*       m,
    const SceneObjectiveId  id,
    const TimeDuration      dur,
    const SceneMissionState result) {
  if (UNLIKELY(m->state != SceneMissionState_Active)) {
    return SceneMissionErr_NotActive;
  }
  SceneObjective* obj = obj_get_mut(m, id);
  if (UNLIKELY(!obj || obj->state != SceneMissionState_Active)) {
    return SceneMissionErr_InvalidObjective;
  }
  if (UNLIKELY(result != SceneMissionState_Success && result != SceneMissionState_Fail)) {
    return SceneMissionErr_InvalidResult;
  }
  if (obj->timeoutDuration == dur && obj->timeoutResult == result) {
    return SceneMissionErr_None;
  }

  log_d(
      "Objective update timeout",
      log_param("id", fmt_int(id)),
      log_param("duration", fmt_duration(dur)),
      log_param("result", fmt_text(scene_mission_state_str(result))));

  obj->timeoutDuration = dur;
  obj->timeoutResult   = result;
  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_obj_end(
    SceneMissionComp* m, const SceneObjectiveId id, const SceneMissionState result) {
  if (UNLIKELY(m->state != SceneMissionState_Active)) {
    return SceneMissionErr_NotActive;
  }
  SceneObjective* obj = obj_get_mut(m, id);
  if (UNLIKELY(!obj || obj->state != SceneMissionState_Active)) {
    return SceneMissionErr_InvalidObjective;
  }
  if (UNLIKELY(result != SceneMissionState_Success && result != SceneMissionState_Fail)) {
    return SceneMissionErr_InvalidResult;
  }

  log_i(
      "Objective end",
      log_param("id", fmt_int(id)),
      log_param("result", fmt_text(scene_mission_state_str(result))));

  obj->state = result;
  return SceneMissionErr_None;
}

const SceneObjective* scene_mission_obj_get(const SceneMissionComp* m, const SceneObjectiveId id) {
  return obj_get(m, id);
}

usize scene_mission_obj_count(const SceneMissionComp* m) { return m->objectives.size; }

usize scene_mission_obj_count_in_state(const SceneMissionComp* m, const SceneMissionState state) {
  usize result = 0;
  dynarray_for_t(&m->objectives, SceneObjective, obj) { result += obj->state == state; }
  return result;
}

const SceneObjective* scene_mission_obj_data(const SceneMissionComp* m) {
  return dynarray_begin_t(&m->objectives, SceneObjective);
}

TimeDuration scene_mission_obj_time(const SceneObjective* obj, const SceneTimeComp* time) {
  if (obj->endTime >= 0) {
    return obj->endTime - obj->startTime;
  }
  return time->time - obj->startTime;
}

TimeDuration scene_mission_obj_time_rem(const SceneObjective* obj, const SceneTimeComp* time) {
  const TimeDuration endTime = obj->endTime >= 0 ? obj->endTime : time->time;
  const TimeDuration elapsed = endTime - obj->startTime;
  if (elapsed >= obj->timeoutDuration) {
    return 0;
  }
  return obj->timeoutDuration - elapsed;
}

TimeDuration scene_mission_obj_time_ended(const SceneObjective* obj, const SceneTimeComp* time) {
  if (obj->endTime < 0) {
    return 0;
  }
  return time->time - obj->endTime;
}
