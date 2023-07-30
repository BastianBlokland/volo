#pragma once
#include "ecs_module.h"

// Forward declare from 'asset_product.h'.
typedef struct sAssetProduct AssetProduct;

typedef enum {
  SceneProductRequest_EnqueueSingle = 1 << 0,
  SceneProductRequest_EnqueueBulk   = 1 << 2,
  SceneProductRequest_CancelSingle  = 1 << 3,
  SceneProductRequest_CancelAll     = 1 << 4,
} SceneProductRequest;

typedef struct {
  const AssetProduct* product;
  u32                 count;
  SceneProductRequest requests;
} SceneProductQueue;

ecs_comp_extern_public(SceneProductionComp) {
  StringHash         productSetId;
  u32                queueCount;
  SceneProductQueue* queues;
};

void scene_product_init(EcsWorld*, String productMapId);
