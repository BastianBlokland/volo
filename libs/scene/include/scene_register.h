#pragma once
#include "ecs_def.h"

enum {
  SceneOrder_TimeUpdate       = -100,
  SceneOrder_NavInit          = -50,
  SceneOrder_SensorUpdate     = -50,
  SceneOrder_ScriptUpdate     = -40,
  SceneOrder_BrainUpdate      = -30,
  SceneOrder_ControllerUpdate = -20,
  SceneOrder_AttachmentUpdate = 25,
  SceneOrder_LocomotionUpdate = 25,
  SceneOrder_VelocityUpdate   = 100,
  SceneOrder_CollisionUpdate  = 100,
  SceneOrder_SelectionUpdate  = 100,
};

/**
 * Register the ecs modules for the Scene library.
 */
void scene_register(EcsDef*);
