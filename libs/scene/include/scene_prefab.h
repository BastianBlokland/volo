#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"
#include "scene_faction.h"

ecs_comp_extern_public(ScenePrefabInstance) { StringHash prefabId; };

/**
 * Create a new prefab resource from the given PrefabMap.
 */
void scene_prefab_init(EcsWorld*, String prefabMapId);

/**
 * Spawn an instance of the given prefab.
 * NOTE: Spawned entity can take multiple frames to initialize.
 */

typedef struct {
  StringHash   prefabId;
  GeoVector    position;
  GeoQuat      rotation;
  SceneFaction faction;
} ScenePrefabSpec;

EcsEntityId scene_prefab_spawn(EcsWorld*, const ScenePrefabSpec*);
