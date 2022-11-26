#pragma once
#include "ecs_def.h"

enum {
  SceneOrder_TimeUpdate       = -100,
  SceneOrder_AttachmentUpdate = 10,
  SceneOrder_LocomotionUpdate = 10,
  SceneOrder_VelocityUpdate   = 100,
  SceneOrder_CollisionUpdate  = 100,
  SceneOrder_SelectionUpdate  = 100,
};

/**
 * Register the ecs modules for the Scene library.
 */
void scene_register(EcsDef*);
