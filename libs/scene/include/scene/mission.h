#pragma once
#include "scene/forward.h"

typedef enum {
  SceneMissionState_Idle,
  SceneMissionState_Active,
  SceneMissionState_Success,
  SceneMissionState_Fail,

  SceneMissionState_Count
} SceneMissionState;

typedef enum {
  SceneMissionErr_None,
  SceneMissionErr_NotActive,
  SceneMissionErr_AlreadyActive,
  SceneMissionErr_InvalidResult,
  SceneMissionErr_InvalidObjective,

  SceneMissionErr_Count,
} SceneMissionErr;

typedef u64 SceneObjectiveId;

typedef struct {
  SceneObjectiveId  id;
  StringHash        nameLoc;
  SceneMissionState state;
  f32               goal, progress;
  TimeDuration      startTime; // -1 until available (potentially delayed until the next tick).
  TimeDuration      endTime;   // -1 until available.
  TimeDuration      timeoutDuration;
  SceneMissionState timeoutResult;
  bool              progressUpdated;
} SceneObjective;

/**
 * Global mission manager.
 */
ecs_comp_extern(SceneMissionComp);

String scene_mission_state_str(SceneMissionState);
String scene_mission_err_str(SceneMissionErr);

void              scene_mission_clear(SceneMissionComp*);
SceneMissionErr   scene_mission_begin(SceneMissionComp*, StringHash name, EcsEntityId instigator);
SceneMissionErr   scene_mission_end(SceneMissionComp*, SceneMissionState result);
SceneMissionState scene_mission_state(const SceneMissionComp*);
StringHash        scene_mission_name(const SceneMissionComp*); // Localization key.
TimeDuration      scene_mission_time(const SceneMissionComp*, const SceneTimeComp*);
TimeDuration      scene_mission_time_ended(const SceneMissionComp*, const SceneTimeComp*);

SceneMissionErr scene_mission_obj_begin(SceneMissionComp*, SceneObjectiveId, StringHash name);
SceneMissionErr scene_mission_obj_goal(SceneMissionComp*, SceneObjectiveId, f32 goal, f32 progress);
SceneMissionErr scene_mission_obj_timeout(
    SceneMissionComp*, SceneObjectiveId, TimeDuration dur, SceneMissionState res);
SceneMissionErr scene_mission_obj_end(SceneMissionComp*, SceneObjectiveId, SceneMissionState res);

const SceneObjective* scene_mission_obj_get(const SceneMissionComp*, SceneObjectiveId);
usize                 scene_mission_obj_count(const SceneMissionComp*);
usize                 scene_mission_obj_count_in_state(const SceneMissionComp*, SceneMissionState);
const SceneObjective* scene_mission_obj_data(const SceneMissionComp*);

TimeDuration scene_mission_obj_time(const SceneObjective*, const SceneTimeComp*);
TimeDuration scene_mission_obj_time_rem(const SceneObjective*, const SceneTimeComp*);
TimeDuration scene_mission_obj_time_ended(const SceneObjective*, const SceneTimeComp*);
