#pragma once
#include "ecs/module.h"
#include "geo/color.h"

ecs_comp_extern_public(SceneLightComp); // Present on all light types.

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
