#pragma once
#include "ecs/module.h"

typedef enum {
  SceneMarkerType_Info,
  SceneMarkerType_Danger,
  SceneMarkerType_Goal,

  SceneMarkerType_Count,
} SceneMarkerType;

ecs_comp_extern_public(SceneMarkerComp) {
  SceneMarkerType type;
  f32             radius;
};

String scene_marker_name(SceneMarkerType);
