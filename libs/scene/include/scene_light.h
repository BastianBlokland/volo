#pragma once
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern_public(SceneLightPointComp) {
  GeoColor radiance;
  f32      radius;
};

ecs_comp_extern_public(SceneLightDirComp) {
  GeoColor radiance;
  bool     shadows, coverage;
};
