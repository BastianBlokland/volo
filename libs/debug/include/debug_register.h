#pragma once
#include "ecs_def.h"

enum {
  DebugOrder_GizmoUpdate         = -500,
  DebugOrder_InspectorToolUpdate = -400,
  DebugOrder_AnimationDebugDraw  = 700,
  DebugOrder_InspectorDebugDraw  = 700,
  DebugOrder_CameraDebugDraw     = 700,
  DebugOrder_GizmoRender         = 725,
  DebugOrder_TextRender          = 750,
  DebugOrder_ShapeRender         = 800,
};

/**
 * Register the ecs modules for the Debug library.
 */
void debug_register(EcsDef*);
