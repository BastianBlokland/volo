#pragma once
#include "ecs_module.h"

typedef struct {
  u8 r, g, b, a;
} AssetTexturePixel4;

typedef struct {
  u8 r;
} AssetTexturePixel1;

ASSERT(sizeof(AssetTexturePixel4) == 4, "Unexpected pixel size");
ASSERT(sizeof(AssetTexturePixel1) == 1, "Unexpected pixel size");

typedef enum {
  AssetTextureChannels_One  = 1,
  AssetTextureChannels_Four = 4,
} AssetTextureChannels;

ecs_comp_extern_public(AssetTextureComp) {
  AssetTextureChannels channels;
  union {
    const u8*                 pixelsRaw;
    const AssetTexturePixel1* pixels1;
    const AssetTexturePixel4* pixels4;
  };
  u32 width, height;
};
