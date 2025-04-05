#pragma once
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern_public(SceneLightPointComp) {
  GeoColor radiance;
  f32      radius;
};

ecs_comp_extern_public(SceneLightSpotComp) {
  GeoColor radiance;
  f32      angle, length;
};

ecs_comp_extern_public(SceneLightLineComp) {
  GeoColor radiance;
  f32      radius, length;
};

ecs_comp_extern_public(SceneLightDirComp) {
  GeoColor radiance;
  bool     shadows, coverage;
};

ecs_comp_extern_public(SceneLightAmbientComp) { f32 intensity; };
