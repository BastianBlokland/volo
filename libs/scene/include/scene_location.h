#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneLocationType_AimTarget,

  SceneLocationType_Count,
} SceneLocationType;

ecs_comp_extern_public(SceneLocationComp) { GeoVector offsets[SceneLocationType_Count]; };
