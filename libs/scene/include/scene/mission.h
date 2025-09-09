#pragma once
#include "ecs/module.h"

typedef enum {
  SceneMissionState_Inactive,
  SceneMissionState_InProgress,
  SceneMissionState_Successful,
  SceneMissionState_Failed,
} SceneMissionState;

typedef enum {
  SceneMissionErr_None,
  SceneMissionErr_NotActive,
  SceneMissionErr_AlreadyActive,
  SceneMissionErr_InvalidResult,
  SceneMissionErr_InvalidObjective,
} SceneMissionErr;

typedef struct {
  StringHash        nameLoc;
  SceneMissionState state;
  f32               goal, progress;
} SceneObjective;

typedef u16 SceneObjectiveId;

/**
 * Global mission manager.
 */
ecs_comp_extern(SceneMissionComp);

void              scene_mission_clear(SceneMissionComp*);
SceneMissionErr   scene_mission_begin(SceneMissionComp*, StringHash nameLoc);
SceneMissionErr   scene_mission_end(SceneMissionComp*, SceneMissionState result);
SceneMissionState scene_mission_state(const SceneMissionComp*);

SceneMissionErr scene_mission_obj_begin(SceneMissionComp*, StringHash name, SceneObjectiveId* out);
SceneMissionErr scene_mission_obj_goal(SceneMissionComp*, SceneObjectiveId, f32 goal, f32 progress);
SceneMissionErr scene_mission_obj_end(SceneMissionComp*, SceneObjectiveId, SceneMissionState res);

const SceneObjective* scene_mission_obj_get(const SceneMissionComp*, SceneObjectiveId);
usize                 scene_mission_obj_count(const SceneMissionComp*);
const SceneObjective* scene_mission_obj_data(const SceneMissionComp*);
