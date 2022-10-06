#pragma once
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

ecs_comp_extern_public(AssetVfxComp) {
  StringHash atlasEntry;
  GeoVector  position;
  GeoQuat    rotation;
  f32        sizeX, sizeY;
  GeoColor   color;
};
