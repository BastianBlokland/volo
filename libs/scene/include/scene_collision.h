#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_sphere.h"
#include "scene.h"

#define scene_query_max_hits 512 // Maximum number of entities that can be hit using a single query.
#define scene_query_stat_count 10

// clang-format off

typedef enum eSceneLayer {
  SceneLayer_Debug                = 1 << 0,
  SceneLayer_Environment          = 1 << 1,
  SceneLayer_InfantryFactionA     = 1 << 2,
  SceneLayer_InfantryFactionB     = 1 << 3,
  SceneLayer_InfantryFactionC     = 1 << 4,
  SceneLayer_InfantryFactionD     = 1 << 5,
  SceneLayer_InfantryFactionNone  = 1 << 6,
  SceneLayer_VehicleFactionA      = 1 << 7,
  SceneLayer_VehicleFactionB      = 1 << 8,
  SceneLayer_VehicleFactionC      = 1 << 9,
  SceneLayer_VehicleFactionD      = 1 << 10,
  SceneLayer_VehicleFactionNone   = 1 << 11,
  SceneLayer_StructureFactionA    = 1 << 12,
  SceneLayer_StructureFactionB    = 1 << 13,
  SceneLayer_StructureFactionC    = 1 << 14,
  SceneLayer_StructureFactionD    = 1 << 15,
  SceneLayer_StructureFactionNone = 1 << 16,
  SceneLayer_Destructible         = 1 << 17,

  SceneLayer_Infantry     = SceneLayer_InfantryFactionA  | SceneLayer_InfantryFactionB  | SceneLayer_InfantryFactionC  | SceneLayer_InfantryFactionD  | SceneLayer_InfantryFactionNone,
  SceneLayer_Vehicle      = SceneLayer_VehicleFactionA   | SceneLayer_VehicleFactionB   | SceneLayer_VehicleFactionC   | SceneLayer_VehicleFactionD   | SceneLayer_VehicleFactionNone,
  SceneLayer_Structure    = SceneLayer_StructureFactionA | SceneLayer_StructureFactionB | SceneLayer_StructureFactionC | SceneLayer_StructureFactionD | SceneLayer_StructureFactionNone,
  SceneLayer_Unit         = SceneLayer_Infantry          | SceneLayer_Vehicle           | SceneLayer_Structure,
  SceneLayer_UnitFactionA = SceneLayer_InfantryFactionA  | SceneLayer_VehicleFactionA   | SceneLayer_StructureFactionA,
  SceneLayer_UnitFactionB = SceneLayer_InfantryFactionB  | SceneLayer_VehicleFactionB   | SceneLayer_StructureFactionB,
  SceneLayer_UnitFactionC = SceneLayer_InfantryFactionC  | SceneLayer_VehicleFactionC   | SceneLayer_StructureFactionC,
  SceneLayer_UnitFactionD = SceneLayer_InfantryFactionD  | SceneLayer_VehicleFactionD   | SceneLayer_StructureFactionD,

  SceneLayer_Count             = 18,
  SceneLayer_None              = 0,
  SceneLayer_AllIncludingDebug = ~0,
  SceneLayer_AllNonDebug       = ~SceneLayer_Debug,
} SceneLayer;

// clang-format on

/**
 * Callback for filtering potential query hits.
 * Return 'true' to keep the target or 'false' to discard the target.
 */
typedef struct {
  const void* context;                                                  // Optional.
  bool (*callback)(const void* context, EcsEntityId tgt, u32 tgtLayer); // Optional.
  SceneLayer layerMask;
} SceneQueryFilter;

/**
 * Global collision environment.
 */
ecs_comp_extern(SceneCollisionEnvComp);
ecs_comp_extern_public(SceneCollisionStatsComp) { i32 queryStats[scene_query_stat_count]; };

typedef enum {
  SceneCollisionType_Sphere,
  SceneCollisionType_Capsule,
  SceneCollisionType_Box,

  SceneCollisionType_Count,
} SceneCollisionType;

typedef struct {
  SceneCollisionType type;
  union {
    GeoSphere     sphere;
    GeoCapsule    capsule;
    GeoBoxRotated box;
  };
} SceneCollisionShape;

ecs_comp_extern_public(SceneCollisionComp) {
  SceneLayer          layer;
  SceneCollisionShape shape;
};

/**
 * Lookup the name of the given layer.
 * Pre-condition: Only a single bit is set.
 */
String scene_layer_name(SceneLayer);

/**
 * Lookup the name of the given collision type.
 */
String scene_collision_type_name(SceneCollisionType);

/**
 * Set a mask to ignore colliders on specific layers globally.
 */
SceneLayer scene_collision_ignore_mask(const SceneCollisionEnvComp*);
void       scene_collision_ignore_mask_set(SceneCollisionEnvComp*, SceneLayer);

/**
 * Add a collision shape to the given entity.
 */

void scene_collision_add_sphere(EcsWorld*, EcsEntityId, GeoSphere, SceneLayer);
void scene_collision_add_capsule(EcsWorld*, EcsEntityId, GeoCapsule, SceneLayer);
void scene_collision_add_box(EcsWorld*, EcsEntityId, GeoBoxRotated, SceneLayer);

/**
 * Intersection apis.
 */

f32 scene_collision_intersect_ray(
    const SceneCollisionComp*, const SceneTransformComp*, const SceneScaleComp*, const GeoRay*);
f32 scene_collision_intersect_ray_shape(
    const SceneCollisionShape*, const SceneTransformComp*, const SceneScaleComp*, const GeoRay*);

/**
 * Query apis.
 */

typedef struct {
  f32         time;
  EcsEntityId entity;
  GeoVector   position;
  GeoVector   normal;
  SceneLayer  layer;
} SceneRayHit;

bool scene_query_ray(
    const SceneCollisionEnvComp*,
    const GeoRay* ray,
    f32           maxDist,
    const SceneQueryFilter*,
    SceneRayHit* out);

bool scene_query_ray_fat(
    const SceneCollisionEnvComp*,
    const GeoRay* ray,
    f32           radius,
    f32           maxDist,
    const SceneQueryFilter*,
    SceneRayHit* out);

/**
 * Query for all objects that are contained in the given sphere.
 * NOTE: Returns the number of hit entities.
 */
u32 scene_query_sphere_all(
    const SceneCollisionEnvComp*,
    const GeoSphere*,
    const SceneQueryFilter*,
    EcsEntityId out[PARAM_ARRAY_SIZE(scene_query_max_hits)]);

/**
 * Query for all objects that are contained in the given sphere.
 * NOTE: Returns the number of hit entities.
 */
u32 scene_query_box_all(
    const SceneCollisionEnvComp*,
    const GeoBoxRotated*,
    const SceneQueryFilter*,
    EcsEntityId out[PARAM_ARRAY_SIZE(scene_query_max_hits)]);

/**
 * Query for all entities that are contained in the frustum formed by the given 8 corner points.
 * NOTE: Returns the number of hit entities.
 */
u32 scene_query_frustum_all(
    const SceneCollisionEnvComp*,
    const GeoVector frustum[PARAM_ARRAY_SIZE(8)],
    const SceneQueryFilter*,
    EcsEntityId out[PARAM_ARRAY_SIZE(scene_query_max_hits)]);

/**
 * Compute geometric shapes for the given collision shapes.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */

GeoSphere
scene_collision_world_sphere(const GeoSphere*, const SceneTransformComp*, const SceneScaleComp*);
GeoCapsule
scene_collision_world_capsule(const GeoCapsule*, const SceneTransformComp*, const SceneScaleComp*);
GeoBoxRotated
scene_collision_world_box(const GeoBoxRotated*, const SceneTransformComp*, const SceneScaleComp*);
GeoBox scene_collision_world_shape(
    const SceneCollisionShape*, const SceneTransformComp*, const SceneScaleComp*);

/**
 * Compute the world axis-aligned bounds for the given collision component.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */
GeoBox scene_collision_world_bounds(
    const SceneCollisionComp*, const SceneTransformComp*, const SceneScaleComp*);

/**
 * Retrieve the query-environment for debug purposes.
 */
const GeoQueryEnv* scene_collision_query_env(const SceneCollisionEnvComp*);
