#pragma once
#include "ecs_module.h"
#include "geo_color.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef struct {
  u8 r;
} AssetTexturePixelB1;

typedef struct {
  u8 r, g, b, a;
} AssetTexturePixelB4;

typedef struct {
  u16 r;
} AssetTexturePixelU1;

typedef struct {
  u16 r, g, b, a;
} AssetTexturePixelU4;

typedef struct {
  f32 r;
} AssetTexturePixelF1;

typedef enum {
  AssetTextureType_U8,
  AssetTextureType_U16,
  AssetTextureType_F32,

  AssetTextureType_Count,
} AssetTextureType;

typedef enum {
  AssetTextureChannels_One  = 1,
  AssetTextureChannels_Four = 4,
} AssetTextureChannels;

typedef enum {
  AssetTextureFlags_Srgb            = 1 << 0,
  AssetTextureFlags_GenerateMipMaps = 1 << 1,
  AssetTextureFlags_CubeMap         = 1 << 2,
  AssetTextureFlags_NormalMap       = 1 << 3,
  AssetTextureFlags_Alpha           = 1 << 4, // Alpha channel is in use.
  AssetTextureFlags_Uncompressed    = 1 << 5, // Texture should not be compressed.
} AssetTextureFlags;

ecs_comp_extern_public(AssetTextureComp) {
  AssetTextureType     type;
  AssetTextureChannels channels;
  AssetTextureFlags    flags;
  union {
    const u8*                  pixelsRaw;
    const AssetTexturePixelB1* pixelsB1;
    const AssetTexturePixelB4* pixelsB4;
    const AssetTexturePixelU1* pixelsU1;
    const AssetTexturePixelU4* pixelsU4;
    const AssetTexturePixelF1* pixelsF1;
  };
  u32 width, height, layers, srcMipLevels, maxMipLevels;
};

String asset_texture_type_str(AssetTextureType);

usize asset_texture_req_mip_size(
    AssetTextureType, AssetTextureChannels, u32 width, u32 height, u32 layers, u32 mipLevel);
usize asset_texture_req_size(
    AssetTextureType, AssetTextureChannels, u32 width, u32 height, u32 layers, u32 mipLevels);
usize asset_texture_req_align(AssetTextureType, AssetTextureChannels);

usize asset_texture_pixel_size(const AssetTextureComp*);
usize asset_texture_mip_size(const AssetTextureComp*, u32 mipLevel);
usize asset_texture_data_size(const AssetTextureComp*);
Mem   asset_texture_data(const AssetTextureComp*);

/**
 * Lookup the color of a specific pixel specified by the given index.
 * NOTE: Always samples mip-level 0.
 */
GeoColor asset_texture_at(const AssetTextureComp*, u32 layer, usize index);

/**
 * Sample the texture at the given normalized x and y coordinates.
 * NOTE: Always samples mip-level 0.
 */
GeoColor asset_texture_sample(const AssetTextureComp*, f32 x, f32 y, u32 layer);

void asset_texture_proc_jsonschema_write(DynString*);
void asset_texture_array_jsonschema_write(DynString*);
