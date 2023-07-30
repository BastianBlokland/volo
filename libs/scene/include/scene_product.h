#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

// Forward declare from 'asset_product.h'.
typedef struct sAssetProduct AssetProduct;

typedef enum {
  SceneProductRequest_EnqueueSingle = 1 << 0,
  SceneProductRequest_EnqueueBulk   = 1 << 2,
  SceneProductRequest_CancelSingle  = 1 << 3,
  SceneProductRequest_CancelAll     = 1 << 4,
} SceneProductRequest;

typedef enum {
  SceneProductState_Idle,
  SceneProductState_Active,
  SceneProductState_Cooldown,
} SceneProductState;

typedef enum {
  SceneProductRallySpace_Local,
  SceneProductRallySpace_World,
} SceneProductRallySpace;

typedef struct {
  const AssetProduct* product;
  u32                 count;
  SceneProductState   state : 8;
  SceneProductRequest requests : 8;
  f32                 progress;
} SceneProductQueue;

ecs_comp_extern_public(SceneProductionComp) {
  StringHash             productSetId;
  u32                    queueCount;
  SceneProductQueue*     queues;
  SceneProductRallySpace rallySpace;
  GeoVector              spawnPos, rallyPos;
};

void scene_product_init(EcsWorld*, String productMapId);
