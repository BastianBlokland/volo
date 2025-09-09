#pragma once
#include "ecs/module.h"

typedef enum {
  SceneMissionState_InProgress,
  SceneMissionState_Successful,
  SceneMissionState_Failed,
} SceneMissionState;

typedef enum {
  SceneMissionErr_None,
  SceneMissionErr_AlreadyActive,
} SceneMissionErr;

typedef struct {
  StringHash nameLoc;
  f32        progress, goal;
} SceneMissionObjective;

typedef u16 SceneObjectiveId;

/**
 * Global mission manager.
 */
ecs_comp_extern(SceneMissionComp);

/**
 * TODO:
 */
SceneMissionErr scene_mission_begin(SceneMissionComp*, StringHash nameLoc);

/**
 * TODO:
 */
SceneMissionErr scene_mission_end(SceneMissionComp*, SceneMissionState result);

/**
 * TODO:
 */
SceneObjectiveId scene_objective_begin(SceneMissionComp*, StringHash nameLoc, f32 goal);

/**
 * TODO:
 */
SceneMissionErr scene_objective_update(SceneMissionComp*, SceneObjectiveId, f32 progress);

/**
 * TODO:
 */
SceneMissionErr scene_objective_end(SceneMissionComp*, SceneObjectiveId, SceneMissionState result);
