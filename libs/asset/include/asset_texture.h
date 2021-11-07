#pragma once
#include "ecs_module.h"

typedef struct {
  float r, g, b, a;
} AssetTexturePixel;

ecs_comp_extern_public(AssetTextureComp) {
  const AssetTexturePixel* pixels;
  u32                      width, height;
};
