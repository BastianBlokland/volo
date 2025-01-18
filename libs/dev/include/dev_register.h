#pragma once
#include "ecs_def.h"

enum {
  DevOrder_TraceQuery          = -1000,
  DevOrder_GizmoUpdate         = -500,
  DevOrder_InspectorToolUpdate = -400,
  DevOrder_RendUpdate          = -400,
  DevOrder_CameraDevDraw       = 50,
  DevOrder_SkeletonDevDraw     = 700,
  DevOrder_InspectorDevDraw    = 740,
  DevOrder_GizmoRender         = 750,
  DevOrder_TextRender          = 750,
  DevOrder_ShapeRender         = 850,
};

/**
 * Register the ecs modules for the Development library.
 */
void dev_register(EcsDef*);
