#pragma once
#include "ecs_def.h"

enum {
  DebugOrder_TraceQuery          = -1000,
  DebugOrder_GizmoUpdate         = -500,
  DebugOrder_InspectorToolUpdate = -400,
  DebugOrder_RendUpdate          = -400,
  DebugOrder_CameraDebugDraw     = 50,
  DebugOrder_AnimationDebugDraw  = 700,
  DebugOrder_InspectorDebugDraw  = 740,
  DebugOrder_GizmoRender         = 750,
  DebugOrder_TextRender          = 750,
  DebugOrder_ShapeRender         = 850,
};

/**
 * Register the ecs modules for the Debug library.
 */
void debug_register(EcsDef*);
