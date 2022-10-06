#pragma once
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern_public(AssetVfxComp) {
  StringHash atlasEntry;
  GeoColor   color;
};
