#pragma once
#include "ecs/module.h"

typedef enum {
  SceneMissionState_Inactive,
  SceneMissionState_InProgress,
  SceneMissionState_Successful,
  SceneMissionState_Failed,

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

typedef struct {
  StringHash        nameLoc;
  SceneMissionState state;
  f32               goal, progress;
  TimeDuration      startTime; // -1 until available (potentially delayed until the next tick).
  TimeDuration      timeoutDuration;
  SceneMissionState timeoutResult;
} SceneObjective;

typedef u16 SceneObjectiveId;

/**
 * Global mission manager.
 */
ecs_comp_extern(SceneMissionComp);

String scene_mission_state_str(SceneMissionState);
String scene_mission_err_str(SceneMissionErr);

void              scene_mission_clear(SceneMissionComp*);
SceneMissionErr   scene_mission_begin(SceneMissionComp*, StringHash name);
SceneMissionErr   scene_mission_end(SceneMissionComp*, SceneMissionState result);
SceneMissionState scene_mission_state(const SceneMissionComp*);
StringHash        scene_mission_name(const SceneMissionComp*); // Localization key.

SceneMissionErr scene_mission_obj_begin(SceneMissionComp*, StringHash name, SceneObjectiveId* out);
SceneMissionErr scene_mission_obj_goal(SceneMissionComp*, SceneObjectiveId, f32 goal, f32 progress);
SceneMissionErr scene_mission_obj_timeout(
    SceneMissionComp*, SceneObjectiveId, TimeDuration rem, SceneMissionState res);
SceneMissionErr scene_mission_obj_end(SceneMissionComp*, SceneObjectiveId, SceneMissionState res);

const SceneObjective* scene_mission_obj_get(const SceneMissionComp*, SceneObjectiveId);
usize                 scene_mission_obj_count(const SceneMissionComp*);
const SceneObjective* scene_mission_obj_data(const SceneMissionComp*);
