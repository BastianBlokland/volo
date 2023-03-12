#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(AssetDecalComp) {
  StringHash colorAtlasEntry;
  f32        width, height;
};
