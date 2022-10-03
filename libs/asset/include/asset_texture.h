#pragma once
#include "ecs_module.h"

typedef struct {
  u8 r;
} AssetTexturePixelB1;

typedef struct {
  u8 r, g, b, a;
} AssetTexturePixelB4;

typedef struct {
  f32 r;
} AssetTexturePixelF1;

typedef struct {
  f32 r, g, b, a;
} AssetTexturePixelF4;

typedef enum {
  AssetTextureType_Byte,
  AssetTextureType_Float,
} AssetTextureType;

typedef enum {
  AssetTextureChannels_One  = 1,
  AssetTextureChannels_Four = 4,
} AssetTextureChannels;

typedef enum {
  AssetTextureFlags_Srgb      = 1 << 0,
  AssetTextureFlags_MipMaps   = 1 << 1,
  AssetTextureFlags_CubeMap   = 1 << 2,
  AssetTextureFlags_NormalMap = 1 << 3,
} AssetTextureFlags;

ecs_comp_extern_public(AssetTextureComp) {
  AssetTextureType     type;
  AssetTextureChannels channels;
  AssetTextureFlags    flags;
  union {
    const u8*                  pixelsRaw;
    const AssetTexturePixelB1* pixelsB1;
    const AssetTexturePixelB4* pixelsB4;
    const AssetTexturePixelF1* pixelsF1;
    const AssetTexturePixelF4* pixelsF4;
  };
  u32 width, height, layers;
};

usize asset_texture_pixel_size(const AssetTextureComp*);
Mem   asset_texture_data(const AssetTextureComp*);

/**
 * Sample the texture at the given normalized x and y coordinates.
 *
 * Pre-condition: texture.type == AssetTextureType_Byte.
 * Pre-condition: texture.channels == AssetTextureChannels_Four.
 */
AssetTexturePixelB4 asset_texture_sample_b4(const AssetTextureComp*, f32 x, f32 y, u32 layer);
