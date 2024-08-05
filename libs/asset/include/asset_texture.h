#pragma once
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_color.h"

typedef enum {
  AssetTextureFormat_u8_r,
  AssetTextureFormat_u8_rgba,
  AssetTextureFormat_u16_r,
  AssetTextureFormat_u16_rgba,
  AssetTextureFormat_f32_r,
  AssetTextureFormat_f32_rgba,

  AssetTextureFormat_Count,
} AssetTextureFormat;

typedef enum {
  AssetTextureFlags_None            = 0,
  AssetTextureFlags_Srgb            = 1 << 0,
  AssetTextureFlags_GenerateMipMaps = 1 << 1,
  AssetTextureFlags_CubeMap         = 1 << 2,
  AssetTextureFlags_NormalMap       = 1 << 3,
  AssetTextureFlags_Alpha           = 1 << 4, // Alpha channel is in use.
  AssetTextureFlags_Uncompressed    = 1 << 5, // Texture should not be compressed.
} AssetTextureFlags;

ecs_comp_extern_public(AssetTextureComp) {
  AssetTextureFormat format;
  AssetTextureFlags  flags;
  u32                width, height, layers, srcMipLevels, maxMipLevels;
  DataMem            pixelData;
};

extern DataMeta g_assetTexDataDef;
extern DataMeta g_assetArrayTexDataDef;
extern DataMeta g_assetProcTexDataDef;

String asset_texture_format_str(AssetTextureFormat);
usize  asset_texture_format_channels(AssetTextureFormat);

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
GeoColor asset_texture_sample_nearest(const AssetTextureComp*, f32 x, f32 y, u32 layer);
