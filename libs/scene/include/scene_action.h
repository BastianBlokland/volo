#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"
#include "scene_bark.h"
#include "scene_faction.h"
#include "script_val.h"

/**
 * TODO:
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
  StringHash  memKey;
  ScriptVal   value;
} SceneActionTell;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
  StringHash  memKey;
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

typedef struct {
  EcsEntityId entity;
  i32         param;
  GeoColor    value;
} SceneActionUpdateLightParam;

typedef struct {
  EcsEntityId entity;
  i32         param;
  f32         value;
} SceneActionUpdateSoundParam;

typedef struct {
  EcsEntityId entity;
  StringHash  layerName;
  i32         param;
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
 * TODO:
 * Document invalidate after next push.
 */
SceneAction* scene_action_push(SceneActionQueueComp*, SceneActionType);
