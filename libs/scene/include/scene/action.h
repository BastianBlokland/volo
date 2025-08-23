#pragma once
#include "ecs/module.h"
#include "geo/color.h"
#include "geo/quat.h"
#include "geo/vector.h"
#include "scene/bark.h"
#include "scene/faction.h"
#include "script/val.h"

/**
 * Per-entity action queue.
 */

typedef enum {
  SceneActionType_Tell,
  SceneActionType_Ask,
  SceneActionType_Spawn,
  SceneActionType_Teleport,
  SceneActionType_NavTravel,
  SceneActionType_NavStop,
  SceneActionType_Attach,
  SceneActionType_Detach,
  SceneActionType_HealthMod,
  SceneActionType_Attack,
  SceneActionType_Bark,
  SceneActionType_UpdateFaction,
  SceneActionType_UpdateSet,
  SceneActionType_UpdateRenderableParam,
  SceneActionType_UpdateVfxParam,
  SceneActionType_UpdateLightParam,
  SceneActionType_UpdateSoundParam,
  SceneActionType_UpdateAnimParam,
} SceneActionType;

typedef struct {
  EcsEntityId entity;
  StringHash  prop;
  ScriptVal   value;
} SceneActionTell;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
  StringHash  prop;
} SceneActionAsk;

typedef struct {
  EcsEntityId  entity;
  StringHash   prefabId;
  f32          scale;
  SceneFaction faction;
  GeoVector    position;
  GeoQuat      rotation;
} SceneActionSpawn;

typedef struct {
  EcsEntityId entity;
  GeoVector   position;
  GeoQuat     rotation;
} SceneActionTeleport;

typedef struct {
  EcsEntityId entity;
  EcsEntityId targetEntity; // If zero: The targetPosition is used instead.
  GeoVector   targetPosition;
} SceneActionNavTravel;

typedef struct {
  EcsEntityId entity;
} SceneActionNavStop;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
  StringHash  jointName;
  GeoVector   offset;
} SceneActionAttach;

typedef struct {
  EcsEntityId entity;
} SceneActionDetach;

typedef struct {
  EcsEntityId entity;
  f32         amount; // Negative for damage, positive for healing.
} SceneActionHealthMod;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
} SceneActionAttack;

typedef struct {
  EcsEntityId   entity;
  SceneBarkType type;
} SceneActionBark;

typedef struct {
  EcsEntityId  entity;
  SceneFaction faction;
} SceneActionUpdateFaction;

typedef struct {
  EcsEntityId entity;
  StringHash  set;
  bool        add;
} SceneActionUpdateSet;

typedef enum {
  SceneActionRenderableParam_Color,
  SceneActionRenderableParam_Alpha,
  SceneActionRenderableParam_Emissive,
} SceneActionRenderableParam;

typedef struct {
  EcsEntityId                entity;
  SceneActionRenderableParam param;
  union {
    f32      value_num;
    GeoColor value_color;
  };
} SceneActionUpdateRenderableParam;

typedef enum {
  SceneActionVfxParam_Alpha,
  SceneActionVfxParam_EmitMultiplier,
} SceneActionVfxParam;

typedef struct {
  EcsEntityId         entity;
  SceneActionVfxParam param;
  f32                 value;
} SceneActionUpdateVfxParam;

typedef enum {
  SceneActionLightParam_Radiance,
  SceneActionLightParam_Length,
  SceneActionLightParam_Angle,
} SceneActionLightParam;

typedef struct {
  EcsEntityId           entity;
  SceneActionLightParam param;
  union {
    GeoColor value_color;
    f32      value_f32;
  };
} SceneActionUpdateLightParam;

typedef enum {
  SceneActionSoundParam_Gain,
  SceneActionSoundParam_Pitch,
} SceneActionSoundParam;

typedef struct {
  EcsEntityId           entity;
  SceneActionSoundParam param;
  f32                   value;
} SceneActionUpdateSoundParam;

typedef enum {
  SceneActionAnimParam_Time,
  SceneActionAnimParam_TimeNorm,
  SceneActionAnimParam_Speed,
  SceneActionAnimParam_Weight,
  SceneActionAnimParam_Active,
  SceneActionAnimParam_Loop,
  SceneActionAnimParam_FadeIn,
  SceneActionAnimParam_FadeOut,
  SceneActionAnimParam_Duration,
} SceneActionAnimParam;

typedef struct {
  EcsEntityId          entity;
  StringHash           layerName;
  SceneActionAnimParam param;
  union {
    f32  value_f32;
    bool value_bool;
  };
} SceneActionUpdateAnimParam;

typedef union {
  SceneActionTell                  tell;
  SceneActionAsk                   ask;
  SceneActionSpawn                 spawn;
  SceneActionTeleport              teleport;
  SceneActionNavTravel             navTravel;
  SceneActionNavStop               navStop;
  SceneActionAttach                attach;
  SceneActionDetach                detach;
  SceneActionHealthMod             healthMod;
  SceneActionAttack                attack;
  SceneActionBark                  bark;
  SceneActionUpdateFaction         updateFaction;
  SceneActionUpdateSet             updateSet;
  SceneActionUpdateRenderableParam updateRenderableParam;
  SceneActionUpdateVfxParam        updateVfxParam;
  SceneActionUpdateLightParam      updateLightParam;
  SceneActionUpdateSoundParam      updateSoundParam;
  SceneActionUpdateAnimParam       updateAnimParam;
} SceneAction;

ecs_comp_extern(SceneActionQueueComp);

SceneActionQueueComp* scene_action_queue_add(EcsWorld*, EcsEntityId entity);

/**
 * Queue an action to be executed at the next 'SceneOrder_ActionUpdate' update.
 * NOTE: Returned pointer is invalidated on the next push to the same queue.
 */
SceneAction* scene_action_push(SceneActionQueueComp*, SceneActionType);
