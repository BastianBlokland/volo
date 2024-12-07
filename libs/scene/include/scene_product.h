#pragma once
#include "asset.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneProductRequest_EnqueueSingle   = 1 << 0,
  SceneProductRequest_EnqueueBulk     = 1 << 2,
  SceneProductRequest_CancelSingle    = 1 << 3,
  SceneProductRequest_CancelAll       = 1 << 4,
  SceneProductRequest_Activate        = 1 << 5,
  SceneProductRequest_PlacementAccept = 1 << 6,
  SceneProductRequest_PlacementCancel = 1 << 7,
} SceneProductRequest;

typedef enum {
  SceneProductState_Idle,
  SceneProductState_Building,
  SceneProductState_Ready,
  SceneProductState_Active,
  SceneProductState_Cooldown,
} SceneProductState;

typedef enum {
  SceneProductFlags_RallyLocalSpace        = 1 << 0,
  SceneProductFlags_PlacementBlocked       = 1 << 1,
  SceneProductFlags_PlacementBlockedWarned = 1 << 2,
  SceneProductFlags_RallyPosUpdated        = 1 << 3,
} SceneProductFlags;

typedef struct {
  const AssetProduct* product;
  u32                 count;
  SceneProductState   state : 8;
  SceneProductRequest requests : 16;
  f32                 progress;
} SceneProductQueue;

ecs_comp_extern_public(SceneProductionComp) {
  StringHash         productSetId;
  SceneProductFlags  flags : 16;
  u16                queueCount;
  SceneProductQueue* queues;
  EcsEntityId        placementPreview;
  EcsEntityId        rallySoundAsset;
  f32                rallySoundGain;
  f32                placementAngle;
  f32                placementRadius;
  GeoVector          spawnPos, rallyPos, placementPos;
};

void scene_product_init(EcsWorld*, String productMapId);

void scene_product_rallypos_set_world(SceneProductionComp*, GeoVector rallyPos);
void scene_product_rallypos_set_local(SceneProductionComp*, GeoVector rallyPos);

bool scene_product_placement_active(const SceneProductionComp*);
void scene_product_placement_accept(SceneProductionComp*);
void scene_product_placement_cancel(SceneProductionComp*);
