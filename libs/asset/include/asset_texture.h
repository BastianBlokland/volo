#pragma once
#include "ecs_module.h"

typedef struct {
  u8 r, g, b, a;
} AssetTexturePixel;

ASSERT(sizeof(AssetTexturePixel) == 4, "Pixels are 4 bytes");

ecs_comp_extern_public(AssetTextureComp) {
  const AssetTexturePixel* pixels;
  u32                      width, height;
};
