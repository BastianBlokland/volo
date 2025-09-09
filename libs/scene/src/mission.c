#include "core/alloc.h"
#include "core/dynarray.h"
#include "ecs/module.h"
#include "ecs/view.h"
#include "ecs/world.h"
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
  if (UNLIKELY(id >= m->objectives.size)) {
    return null;
  }
  return dynarray_at_t(&m->objectives, id, SceneObjective);
}

static SceneObjective* obj_get_mut(SceneMissionComp* m, const SceneObjectiveId id) {
  return (SceneObjective*)obj_get(m, id);
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

  (void)mission;
  (void)time;
}

ecs_module_init(scene_mission_module) {
  ecs_register_comp(SceneMissionComp, .destructor = ecs_destruct_mission);

  ecs_register_view(UpdateGlobalView);

  ecs_register_system(SceneMissionUpdateSys, ecs_view_id(UpdateGlobalView));
}

SceneMissionState scene_mission_state(const SceneMissionComp* m) { return m->state; }

void scene_mission_clear(SceneMissionComp* m) {
  m->state = SceneMissionState_Inactive;
  m->name  = 0;
  dynarray_clear(&m->objectives);
}

SceneMissionErr scene_mission_begin(SceneMissionComp* m, const StringHash nameLoc) {
  if (UNLIKELY(m->state == SceneMissionState_InProgress)) {
    return SceneMissionErr_AlreadyActive;
  }
  scene_mission_clear(m);

  m->name  = nameLoc;
  m->state = SceneMissionState_InProgress;
  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_end(SceneMissionComp* m, const SceneMissionState result) {
  if (UNLIKELY(m->state != SceneMissionState_InProgress)) {
    return SceneMissionErr_NotActive;
  }
  if (UNLIKELY(result != SceneMissionState_Successful && result != SceneMissionState_Failed)) {
    return SceneMissionErr_InvalidResult;
  }
  m->state = result;
  return SceneMissionErr_None;
}

SceneMissionErr
scene_mission_obj_begin(SceneMissionComp* m, const StringHash nameLoc, SceneObjectiveId* out) {
  if (UNLIKELY(m->state != SceneMissionState_InProgress)) {
    return SceneMissionErr_NotActive;
  }
  *out = m->objectives.size;

  SceneObjective* obj = dynarray_push_t(&m->objectives, SceneObjective);

  *obj = (SceneObjective){
      .nameLoc  = nameLoc,
      .state    = SceneMissionState_InProgress,
      .goal     = -1.0f,
      .progress = -1.0f,
  };

  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_obj_goal(
    SceneMissionComp* m, const SceneObjectiveId id, const f32 goal, const f32 progress) {
  if (UNLIKELY(m->state != SceneMissionState_InProgress)) {
    return SceneMissionErr_NotActive;
  }
  SceneObjective* obj = obj_get_mut(m, id);
  if (UNLIKELY(!obj || obj->state != SceneMissionState_InProgress)) {
    return SceneMissionErr_InvalidObjective;
  }
  obj->goal     = goal;
  obj->progress = progress;
  return SceneMissionErr_None;
}

SceneMissionErr scene_mission_obj_end(
    SceneMissionComp* m, const SceneObjectiveId id, const SceneMissionState result) {
  if (UNLIKELY(m->state != SceneMissionState_InProgress)) {
    return SceneMissionErr_NotActive;
  }
  SceneObjective* obj = obj_get_mut(m, id);
  if (UNLIKELY(!obj || obj->state != SceneMissionState_InProgress)) {
    return SceneMissionErr_InvalidObjective;
  }
  if (UNLIKELY(result != SceneMissionState_Successful && result != SceneMissionState_Failed)) {
    return SceneMissionErr_InvalidResult;
  }

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
