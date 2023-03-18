#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(AssetDecalComp) {
  StringHash colorAtlasEntry;
  StringHash normalAtlasEntry; // Optional, 0 if unused.
  f32        roughness;
  f32        width, height;
  f32        thickness;
};
