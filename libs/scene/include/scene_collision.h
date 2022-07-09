#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

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

void scene_collision_add_capsule(EcsWorld*, EcsEntityId entity, SceneCollisionCapsule);
