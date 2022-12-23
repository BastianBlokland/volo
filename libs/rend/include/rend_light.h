#pragma once
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"

ecs_comp_extern_public(RendLightGlobalComp) {
  GeoColor sunLight;
  f32      sunShininess;
  GeoQuat  sunRotation;
  f32      ambientIntensity;
  f32      reflectFrac;
};

void rend_light_global_to_default(RendLightGlobalComp*);
