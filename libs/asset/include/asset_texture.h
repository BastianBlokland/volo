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

typedef struct {
  f32 r, g, b, a;
} AssetTexturePixelF4;

typedef struct {
  f32 r;
} AssetTexturePixelF1;

ASSERT(sizeof(AssetTexturePixelF4) == sizeof(f32) * 4, "Unexpected float pixel size");
ASSERT(sizeof(AssetTexturePixelF1) == sizeof(f32), "Unexpected float pixel size");

typedef enum {
  AssetTextureType_Byte,
  AssetTextureType_Float,
} AssetTextureType;

typedef enum {
  AssetTextureChannels_One  = 1,
  AssetTextureChannels_Four = 4,
} AssetTextureChannels;

ecs_comp_extern_public(AssetTextureComp) {
  AssetTextureType     type;
  AssetTextureChannels channels;
  union {
    const u8*                  pixelsRaw;
    const AssetTexturePixelB1* pixelsB1;
    const AssetTexturePixelB4* pixelsB4;
    const AssetTexturePixelF1* pixelsF1;
    const AssetTexturePixelF4* pixelsF4;
  };
  u32 width, height;
};

usize asset_texture_pixel_size(const AssetTextureComp*);
Mem   asset_texture_data(const AssetTextureComp*);
