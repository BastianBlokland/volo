#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global product resource.
 */
ecs_comp_extern(SceneProductResourceComp);

/**
 * Production component.
 */
ecs_comp_extern_public(SceneProductionComp) { StringHash productSetId; };

/**
 * Create a new product resource from the given ProductMap.
 */
void scene_product_init(EcsWorld*, String productMapId);

/**
 * Retrieve the asset entity of the global product-map.
 */
EcsEntityId scene_product_map(const SceneProductResourceComp*);
