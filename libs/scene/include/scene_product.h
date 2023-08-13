#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

// Forward declare from 'asset_product.h'.
typedef struct sAssetProduct AssetProduct;

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
  SceneProductFlags_RallyLocalSpace  = 1 << 0,
  SceneProductFlags_PlacementBlocked = 1 << 1,
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
  GeoVector          spawnPos, rallyPos, placementPos;
};

void scene_product_init(EcsWorld*, String productMapId);

bool scene_product_placement_active(const SceneProductionComp*);
void scene_product_placement_accept(SceneProductionComp*);
void scene_product_placement_cancel(SceneProductionComp*);
