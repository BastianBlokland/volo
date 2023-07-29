#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global product resource.
 */
ecs_comp_extern(SceneProductResourceComp);

typedef struct {
  u32 productIndex;
} SceneProductQueue;

ecs_comp_extern_public(SceneProductionComp) {
  StringHash         productSetId;
  u32                queueCount;
  SceneProductQueue* queues;
};

/**
 * Create a new product resource from the given ProductMap.
 */
void scene_product_init(EcsWorld*, String productMapId);

/**
 * Retrieve the asset entity of the global product-map.
 */
EcsEntityId scene_product_map(const SceneProductResourceComp*);
