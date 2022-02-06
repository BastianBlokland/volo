#pragma once
#include "ecs_module.h"

typedef struct {
  u8 r, g, b, a;
} AssetTexturePixelB4;

typedef struct {
  u8 r;
} AssetTexturePixelB1;

ASSERT(sizeof(AssetTexturePixelB4) == 4, "Unexpected byte pixel size");
ASSERT(sizeof(AssetTexturePixelB1) == 1, "Unexpected byte pixel size");

typedef enum {
  AssetTextureChannels_One  = 1,
  AssetTextureChannels_Four = 4,
} AssetTextureChannels;

ecs_comp_extern_public(AssetTextureComp) {
  AssetTextureChannels channels;
  union {
    const u8*                  pixelsRaw;
    const AssetTexturePixelB1* pixelsB1;
    const AssetTexturePixelB4* pixelsB4;
  };
  u32 width, height;
};
