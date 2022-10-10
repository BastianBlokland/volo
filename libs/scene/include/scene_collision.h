#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_ray.h"
#include "geo_sphere.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);
ecs_comp_extern(SceneScaleComp);

/**
 * Maximum number of entities that can be hit using a single query, additional entities are ignored.
 */
#define scene_query_max_hits 128

typedef enum {
  SceneLayer_Debug        = 1 << 0,
  SceneLayer_Environment  = 1 << 1,
  SceneLayer_UnitFactionA = 1 << 2,
  SceneLayer_UnitFactionB = 1 << 3,
  SceneLayer_UnitFactionC = 1 << 4,
  SceneLayer_UnitFactionD = 1 << 5,
  SceneLayer_Unit = SceneLayer_UnitFactionA | SceneLayer_UnitFactionB | SceneLayer_UnitFactionC |
                    SceneLayer_UnitFactionD,

  SceneLayer_Count = 6,
  SceneLayer_None  = 0,
  SceneLayer_All   = ~0,
} SceneLayer;

/**
 * Callback for filtering query hits.
 * Return 'true' to keep the hit or 'false' to discard the hit.
 */
typedef struct {
  const void* context;                                       // Optional.
  bool (*callback)(const void* context, EcsEntityId entity); // Optional.
  SceneLayer layerMask;
} SceneQueryFilter;

/**
 * Global collision environment.
 */
ecs_comp_extern(SceneCollisionEnvComp);

typedef enum {
  SceneCollisionType_Sphere,
  SceneCollisionType_Capsule,
  SceneCollisionType_Box,

  SceneCollisionType_Count,
} SceneCollisionType;

typedef enum {
  SceneCollision_Up,
  SceneCollision_Forward,
  SceneCollision_Right,
} SceneCollisionDir;

typedef struct {
  GeoVector offset;
  f32       radius;
} SceneCollisionSphere;

typedef struct {
  GeoVector         offset;
  SceneCollisionDir dir;
  f32               radius;
  f32               height;
} SceneCollisionCapsule;

typedef struct {
  GeoVector min;
  GeoVector max;
} SceneCollisionBox;

ecs_comp_extern_public(SceneCollisionComp) {
  SceneCollisionType type;
  SceneLayer         layer;
  union {
    SceneCollisionSphere  sphere;
    SceneCollisionCapsule capsule;
    SceneCollisionBox     box;
  };
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
 * Add a collision shape to the given entity.
 */

void scene_collision_add_sphere(EcsWorld*, EcsEntityId, SceneCollisionSphere, SceneLayer);
void scene_collision_add_capsule(EcsWorld*, EcsEntityId, SceneCollisionCapsule, SceneLayer);
void scene_collision_add_box(EcsWorld*, EcsEntityId, SceneCollisionBox, SceneLayer);

/**
 * Query apis.
 */

typedef struct {
  EcsEntityId entity;
  GeoVector   position;
  GeoVector   normal;
  f32         time;
} SceneRayHit;

bool scene_query_ray(
    const SceneCollisionEnvComp*, const GeoRay* ray, const SceneQueryFilter*, SceneRayHit* out);

/**
 * Query for all entities that are contained in the frustum formed by the given 8 corner points.
 * NOTE: Returns the number of hit entities.
 */
u32 scene_query_frustum_all(
    const SceneCollisionEnvComp*,
    const GeoVector frustum[8],
    const SceneQueryFilter*,
    EcsEntityId out[scene_query_max_hits]);

/**
 * Compute geometric shapes for the given collision shapes.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */

GeoSphere scene_collision_world_sphere(
    const SceneCollisionSphere*, const SceneTransformComp*, const SceneScaleComp*);
GeoCapsule scene_collision_world_capsule(
    const SceneCollisionCapsule*, const SceneTransformComp*, const SceneScaleComp*);
GeoBoxRotated scene_collision_world_box(
    const SceneCollisionBox*, const SceneTransformComp*, const SceneScaleComp*);

/**
 * Compute the world axis-aligned bounds for the given collision component.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */
GeoBox scene_collision_world_bounds(
    const SceneCollisionComp*, const SceneTransformComp*, const SceneScaleComp*);
