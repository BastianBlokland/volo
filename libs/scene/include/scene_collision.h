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

/**
 * Global collision environment.
 */
ecs_comp_extern(SceneCollisionEnvComp);

typedef enum {
  SceneCollisionType_Sphere,
  SceneCollisionType_Capsule,
  SceneCollisionType_Box,
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
  union {
    SceneCollisionSphere  sphere;
    SceneCollisionCapsule capsule;
    SceneCollisionBox     box;
  };
};

/**
 * Add a collision shape to the given entity.
 */

void scene_collision_add_sphere(EcsWorld*, EcsEntityId, SceneCollisionSphere);
void scene_collision_add_capsule(EcsWorld*, EcsEntityId, SceneCollisionCapsule);
void scene_collision_add_box(EcsWorld*, EcsEntityId, SceneCollisionBox);

/**
 * Query apis.
 */

typedef struct {
  EcsEntityId entity;
  GeoVector   position;
  GeoVector   normal;
  f32         time;
} SceneRayHit;

bool scene_query_ray(const SceneCollisionEnvComp*, const GeoRay* ray, SceneRayHit* out);

/**
 * Query for all entities that are contained in the frustum formed by the given 8 corner points.
 * NOTE: Returns the number of hit entities.
 */
u32 scene_query_frustum_all(
    const SceneCollisionEnvComp*,
    const GeoVector frustum[8],
    EcsEntityId     out[scene_query_max_hits]);

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
