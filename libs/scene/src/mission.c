#include "core/alloc.h"
#include "core/array.h"
#include "core/dynarray.h"
#include "ecs/module.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "log/logger.h"
#include "scene/mission.h"
#include "scene/time.h"

ecs_comp_define(SceneMissionComp) {
  StringHash        name; // Localization key.
  SceneMissionState state;
  DynArray          objectives; // SceneMissionObjective[].
};

static void ecs_destruct_mission(void* data) {
  SceneMissionComp* comp = data;
  dynarray_destroy(&comp->objectives);
}

static SceneMissionComp* mission_init(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneMissionComp,
      .objectives = dynarray_create_t(g_allocHeap, SceneObjective, 32));
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

static void obj_update(SceneMissionComp* m, SceneObjective* obj, const SceneTimeComp* time) {
  (void)m;
  if (obj->startTime < 0) {
    obj->startTime = time->time;
  }
  if (obj->state != SceneMissionState_Active) {
    if (obj->endTime < 0) {
      obj->endTime = time->time;
    }
    return;
  }
  const TimeDuration timeElapsed = time->time - obj->startTime;
  if (obj->timeoutDuration > 0 && timeElapsed >= obj->timeoutDuration) {
    obj->state = obj->timeoutResult;
  }
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_maybe_write(SceneMissionComp);
  ecs_access_read(SceneTimeComp);
}

ecs_system_define(SceneMissionUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  SceneMissionComp* mission = ecs_view_write_t(globalItr, SceneMissionComp);
  if (UNLIKELY(!mission)) {
    mission = mission_init(world);
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  switch (mission->state) {
  case SceneMissionState_Active:
    dynarray_for_t(&mission->objectives, SceneObjective, obj) { obj_update(mission, obj, time); }
    break;
  default:
    break;
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
  m->state = SceneMissionState_Idle;
  m->name  = 0;
  dynarray_clear(&m->objectives);
}

SceneMissionErr scene_mission_begin(SceneMissionComp* m, const StringHash name) {
  if (UNLIKELY(m->state == SceneMissionState_Active)) {
    return SceneMissionErr_AlreadyActive;
  }
  scene_mission_clear(m);

  log_i("Mission begin");

  m->name  = name;
  m->state = SceneMissionState_Active;
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

  log_d(
      "Objective update goal",
      log_param("id", fmt_int(id)),
      log_param("goal", fmt_float(goal)),
      log_param("progress", fmt_float(progress)));

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

  log_d(
      "Objective set timeout",
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
  if (obj->endTime >= 0) {
    return 0;
  }
  const TimeDuration elapsed = time->time - obj->startTime;
  if (elapsed >= obj->timeoutDuration) {
    return 0;
  }
  return obj->timeoutDuration - elapsed;
}
