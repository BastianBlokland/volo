#pragma once
#include "ecs/module.h"
#include "geo/quat.h"
#include "scene/faction.h"
#include "script/val.h"

typedef enum {
  ScenePrefabVariant_Normal,
  ScenePrefabVariant_Preview,
  ScenePrefabVariant_Edit,

  ScenePrefabVariant_Count,
} ScenePrefabVariant;

/**
 * Global prefab resources.
 */
ecs_comp_extern(ScenePrefabEnvComp);

/**
 * Component on a prefab instance.
 */
ecs_comp_extern_public(ScenePrefabInstanceComp) {
  u32                id; // Optional persistent id.
  StringHash         prefabId;
  u32                assetHash; // Hash of the prefab asset content.
  ScenePrefabVariant variant : 8;
  bool               isVolatile; // Prefab should not be persisted.
  u16                assetFlags; // AssetPrefabFlags.
};

/**
 * Create a new prefab resource from the given PrefabMap.
 */
void scene_prefab_init(EcsWorld*, String prefabMapId);

/**
 * Retrieve the asset entity of the global prefab map.
 */
EcsEntityId scene_prefab_map(const ScenePrefabEnvComp*);

/**
 * Retrieve the prefab-map's version number.
 * Version is incremented when the map is updated and can be used to invalidate cached data.
 */
u32 scene_prefab_map_version(const ScenePrefabEnvComp*);

/**
 * Spawn an instance of the given prefab.
 * NOTE: Spawned entity can take multiple frames to initialize.
 */

typedef enum {
  ScenePrefabFlags_Volatile      = 1 << 0, // Prefab should not be persisted.
  ScenePrefabFlags_SnapToTerrain = 1 << 1,
} ScenePrefabFlags;

typedef struct {
  StringHash key;
  ScriptVal  value;
} ScenePrefabProperty;

typedef struct {
  u32                        id; // Optional persistent id.
  StringHash                 prefabId;
  SceneFaction               faction;
  f32                        scale;
  ScenePrefabVariant         variant : 8;
  ScenePrefabFlags           flags : 8;
  u16                        propertyCount;
  const ScenePrefabProperty* properties;
  GeoVector                  position;
  GeoQuat                    rotation;
  StringHash                 sets[8];
} ScenePrefabSpec;

EcsEntityId scene_prefab_spawn(EcsWorld*, const ScenePrefabSpec*);
void        scene_prefab_spawn_onto(ScenePrefabEnvComp*, const ScenePrefabSpec*, EcsEntityId);
void        scene_prefab_spawn_replace(ScenePrefabEnvComp*, const ScenePrefabSpec*, EcsEntityId);
