#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_capsule.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);
ecs_comp_extern(SceneScaleComp);

typedef enum { SceneCollisionType_Capsule } SceneCollisionType;

typedef enum {
  SceneCollisionCapsule_Up,
  SceneCollisionCapsule_Forward,
  SceneCollisionCapsule_Right,
} SceneCollisionCapsuleDir;

typedef struct {
  GeoVector                offset;
  SceneCollisionCapsuleDir direction;
  f32                      radius;
  f32                      height;
} SceneCollisionCapsule;

ecs_comp_extern_public(SceneCollisionComp) {
  SceneCollisionType type;
  union {
    SceneCollisionCapsule data_capsule;
  };
};

/**
 * Add capsule collision to the given entity.
 */
void scene_collision_add_capsule(EcsWorld*, EcsEntityId entity, SceneCollisionCapsule);

/**
 * Compute a geometric capsule for the given capsule collision shape.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */
GeoCapsule scene_collision_world_capsule(
    const SceneCollisionCapsule*, const SceneTransformComp*, const SceneScaleComp*);
