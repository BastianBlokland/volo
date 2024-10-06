#pragma once
#include "ecs_def.h"

enum {
  SceneOrder_TimeUpdate       = -100,
  SceneOrder_NavInit          = -50,
  SceneOrder_SetInit          = -50,
  SceneOrder_CollisionInit    = -50,
  SceneOrder_ScriptUpdate     = -41,
  SceneOrder_ActionUpdate     = -40,
  SceneOrder_AttachmentUpdate = 25,
  SceneOrder_LocomotionUpdate = 25,
  SceneOrder_VelocityUpdate   = 100,
  SceneOrder_SetUpdate        = 800,
};

/**
 * Register the ecs modules for the Scene library.
 */
void scene_register(EcsDef*);
